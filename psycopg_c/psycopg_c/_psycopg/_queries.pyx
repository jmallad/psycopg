"""
Utility module to manipulate queries
"""

# Copyright (C) 2020 The Psycopg Team

import re
from typing import Any, Dict, List, Mapping, Match, NamedTuple, Optional
from typing import Sequence, Tuple, Union, TYPE_CHECKING
from functools import lru_cache

from . import pq
from . import errors as e
from .sql import Composable
from .abc import Buffer, Query, Params
from ._enums import PyFormat
from ._encodings import conn_encoding
from libc.stdlib cimport malloc, free
from libc.string cimport memset, memcpy
from cython.unicode import PyUnicode_AsWideCharString, PyUnicode_GET_LENGTH, \
PyUnicode_DATA, PyUnicode_FromKindAndData

cdef extern from "stdio.h":
    struct FILE
    int fprintf(FILE *stream, const char *format, ...);
    int snprintf(char *str, size_t size, const char *format, ...);
    FILE* stderr    
cdef extern from "errno.h":
    int errno

cdef union query_item:
        int data_int
        void* data_bytes

cdef struct query_part:
        void* pre
        unsigned pre_len
        query_item item
        item_type_enum item_type
        unsigned data_len
        char format

cdef extern from "placeholders.c":
    int escaped_len(unsigned char*, unsigned)
    int escape(unsigned char* out,
	   unsigned outlen,
	   unsigned char*,
	   unsigned inlen);
    int escape_m(unsigned char** out,
	     unsigned* outlen,
	     unsigned char*,
	     unsigned inlen);
    int count_placeholders(unsigned char*,
		       unsigned inlen);
    int search_placeholders(query_part* out,
			unsigned outlen,
			unsigned char*,
			unsigned inlen);
    const char* placeholder_strerror(int err);

cdef enum item_type_enum:
    ITEM_INT = 0,
    ITEM_STR = 1
    
c_exc_types = {
    SUCCESS
}
        
class QueryPart(NamedTuple):
    pre: bytes
    item: Union[int, str]
    format: PyFormat

cdef class PostgresQuery():
    """
    Helper to convert a Python query and parameters into Postgres format.
    """

    cdef bytes query
    cdef object params
    cdef Transformer _tx
    cdef tuple types
    cdef list _want_formats
    cdef list formats
    cdef str _encoding
    cdef list _order

    #Old
    cdef list _parts
    #New
    cdef query_part* parts

    
    def __cinit__(self, transformer: "Transformer"):
        self._tx = transformer
        self.parts = new_list()
        
        self.params: Optional[Sequence[Optional[Buffer]]] = None
        # these are tuples so they can be used as keys e.g. in prepared stmts
        self.types: Tuple[int, ...] = ()

        # The format requested by the user and the ones to really pass Postgres
        self._want_formats: Optional[List[PyFormat]] = None
        self.formats: Optional[Sequence[pq.Format]] = None

        self._encoding = conn_encoding(transformer.connection)
        self.query = b""
        self._order: Optional[List[str]] = None

    cpdef convert(self, query: Query, vars: Optional[Params]):
        """
        Set up the query and parameters to convert.

        The results of this function can be obtained accessing the object
        attributes (`query`, `params`, `types`, `formats`).
        """
        if isinstance(query, str):
            bquery = query.encode(self._encoding)
        elif isinstance(query, Composable):
            bquery = query.as_bytes(self._tx)
        else:
            bquery = query

        #Get raw pointer to python string, and length in bytes
        cdef void* rawquery = PyUnicode_DATA(bquery)
        cdef unsigned rawlen = PyUnicode_GET_LENGTH(bquery)
        #Count number of placeholders in the string
        cdef int n_placeholders = count_placeholders(rawquery, rawlen)
        #Error message objects
        cdef char* errmsg
        cdef str errobj
        #Allocate array for query parts
        self.parts = malloc(sizeof(query_part) * n_placeholders)        
        if not parts:
            raise MemoryError("Dynamic allocation failure")
        #Split query into the array of query parts
        cdef int ret = _split_query(self.parts,
                           n_placeholders,
                           rawquery,
                           rawlen,
                           self._encoding)
        if ret < 0:
            errmsg = placeholder_strerror(ret);
            errobj = PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND,
                                                        strlen(errmsg))
            raise ValueError(f"Error parsing query: {errobj}",)
        #Convert array back to string here, in Postgres format
        #...
        #Un-escape double percents
        #...
        if vars is not None:
            (
                self.query,
                self._want_formats,
                self._order,
            ) = _query2pg(self.parts, n_placeholders, bquery, self._encoding)
        else:
            self.query = bquery
            self._want_formats = self._order = None

        self.dump(vars)

    @classmethod
    def dump(self, vars: Optional[Params]):
        """
        Process a new set of variables on the query processed by `convert()`.

        This method updates `params` and `types`.
        """
        if vars is not None:
            params = _validate_and_reorder_params(self._parts, vars, self._order)
            assert self._want_formats is not None
            self.params = self._tx.dump_sequence(params, self._want_formats)
            self.types = self._tx.types or ()
            self.formats = self._tx.formats
        else:
            self.params = None
            self.types = ()
            self.formats = None

cdef class PostgresClientQuery(PostgresQuery):
    """
    PostgresQuery subclass merging query and arguments client-side.
    """

    cdef bytes template;

    cpdef convert(self, query: Query, vars: Optional[Params]):
        """
        Set up the query and parameters to convert.

        The results of this function can be obtained accessing the object
        attributes (`query`, `params`, `types`, `formats`).
        """
        if isinstance(query, str):
            bquery = query.encode(self._encoding)
        elif isinstance(query, Composable):
            bquery = query.as_bytes(self._tx)
        else:
            bquery = query

        if vars is not None:
            (self.template, self._order, self._parts) = _query2pg_client(
                bquery, self._encoding
            )
        else:
            self.query = bquery
            self._order = None

        self.dump(vars)

    @classmethod
    def dump(self, vars: Optional[Params]):
        """
        Process a new set of variables on the query processed by `convert()`.

        This method updates `params` and `types`.
        """
        if vars is not None:
            params = _validate_and_reorder_params(self._parts, vars, self._order)
            self.params = tuple(
                self._tx.as_literal(p) if p is not None else b"NULL" for p in params)
            self.query = self.template % self.params
        else:
            self.params = None

#@lru_cache()
#Returns Tuple[bytes, List[PyFormat], Optional[List[str]], List[QueryPart]]:
cdef tuple _query2pg(
    query_part* parts, unsigned n_parts, query, encoding: str
) except NULL:
    """
    Convert Python query and params into something Postgres understands.

    - Convert Python placeholders (``%s``, ``%(name)s``) into Postgres
      format (``$1``, ``$2``)
    - placeholders can be %s, %t, or %b (auto, text or binary)
    - return ``query`` (bytes), ``formats`` (list of formats) ``order``
      (sequence of names used in the query, in the position they appear)
      ``parts`` (splits of queries and placeholders).
    """
    if not parts:
        raise MemoryError("Null pointer dereference on 'parts'")
    cdef char** order = NULL
    cdef char** chunks = NULL
    cdef char** formats = NULL



    cdef char cbuf[128] #Conversion buffer
    memset(cbuf, 0, 128)
    
    cdef query_part* qp = <query_part*>i.data

    if not i:
        free_lists(resources, 3)
    
    cdef int slen
    
    qp = <query_part*>i.data
    if not qp:
        free_lists(resources, 3)
    if qp.item_type == ITEM_INT:
        while qp:
            list_append(chunks, qp.pre, qp.pre_len, 0)
            slen = snprintf(cbuf, 128, "$%d", (qp.item.data_int + 1))
            if slen < 0:
                fprintf(stderr, "%s", strerror(errno))
                return TypeError("snprintf failed")
            list_append(chunks, cbuf, slen, 1)
            list_append(formats, &qp.format, 1, 0)
            i = i.next
            # Stop at second-to-last element
            if not i.next:
                break
            qp = <query_part*>i.data
    elif qp.item_type == ITEM_STR:
        seen: Dict[str, Tuple[bytes, PyFormat]] = {}
        while qp:
        #for part in parts[:-1]:
            list_append(chunks, qp.pre, qp.pre_len, 0)
            if part.item not in seen:
                ph = b"$%d" % (len(seen) + 1)
                seen[part.item] = (ph, part.format)
                order.append(qp.item.data_bytes)
                chunks.append(ph)
                formats.append(part.format)
            else:
                if seen[part.item][1] != part.format:
                    raise e.ProgrammingError(
                        f"placeholder '{part.item}' cannot have different formats"
                    )
                chunks.append(seen[part.item][0])

    # last part
    chunks.append(parts[-1].pre)

    return b"".join(chunks), formats, order, parts


#Returns Tuple[bytes, Optional[List[str]], List[QueryPart]]
#@lru_cache()
cdef _query2pg_client(
    query: bytes, encoding: str
):
    """
    Convert Python query and params into a template to perform client-side binding
    """
    parts = _split_query(query, encoding, collapse_double_percent=False)
    order: Optional[List[str]] = None
    chunks: List[bytes] = []

    if isinstance(parts[0].item, int):
        for part in parts[:-1]:
            assert isinstance(part.item, int)
            chunks.append(part.pre)
            chunks.append(b"%s")

    elif isinstance(parts[0].item, str):
        seen: Dict[str, Tuple[bytes, PyFormat]] = {}
        order = []
        for part in parts[:-1]:
            assert isinstance(part.item, str)
            chunks.append(part.pre)
            if part.item not in seen:
                ph = b"%s"
                seen[part.item] = (ph, part.format)
                order.append(part.item)
                chunks.append(ph)
            else:
                chunks.append(seen[part.item][0])
                order.append(part.item)

    # last part
    chunks.append(parts[-1].pre)

    return b"".join(chunks), order, parts

#Returns Sequence[Any]
cdef _validate_and_reorder_params(
    parts: List[QueryPart], vars: Params, order: Optional[List[str]]
):
    """
    Verify the compatibility between a query and a set of params.
    """
    # Try concrete types, then abstract types
    t = type(vars)
    if t is list or t is tuple:
        sequence = True
    elif t is dict:
        sequence = False
    elif isinstance(vars, Sequence) and not isinstance(vars, (bytes, str)):
        sequence = True
    elif isinstance(vars, Mapping):
        sequence = False
    else:
        raise TypeError(
            "query parameters should be a sequence or a mapping,"
            f" got {type(vars).__name__}"
        )

    if sequence:
        if len(vars) != len(parts) - 1:
            raise e.ProgrammingError(
                f"the query has {len(parts) - 1} placeholders but"
                f" {len(vars)} parameters were passed"
            )
        if vars and not isinstance(parts[0].item, int):
            raise TypeError("named placeholders require a mapping of parameters")
        return vars  # type: ignore[return-value]

    else:
        if vars and len(parts) > 1 and not isinstance(parts[0][1], str):
            raise TypeError(
                "positional placeholders (%s) require a sequence of parameters"
            )
        try:
            return [vars[item] for item in order or ()]  # type: ignore[call-overload]
        except KeyError:
            raise e.ProgrammingError(
                "query parameter missing:"
                f" {', '.join(sorted(i for i in order or () if i not in vars))}"
            )

_re_placeholder = re.compile(
    rb"""(?x)
        %                       # a literal %
        (?:
            (?:
                \( ([^)]+) \)   # or a name in (braces)
                .               # followed by a format
            )
            |
            (?:.)               # or any char, really
        )
        """
)

cdef list _split_query(
    query_part* out,
    unsigned n_parts,
    char* query,
    unsigned querylen,
    encoding: str = "ascii",
    collapse_double_percent: bool = True
):
    #Parse placeholders into output array
    cdef int ret = search_placeholders(out, n_parts, query, querylen);
    return ret

_ph_to_fmt = {
    b"s": PyFormat.AUTO,
    b"t": PyFormat.TEXT,
    b"b": PyFormat.BINARY,
}
