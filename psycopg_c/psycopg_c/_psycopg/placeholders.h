#pragma once

enum {
    ITEM_INT = 0,
    ITEM_STR = 1
};

enum {
	PH_KWD = 1, /* Keyword placeholder */
	PH_POS = 2  /* Positional placeholder */
};

enum {
	SUCCESS   = 0,  /* No error */
	ENULLPTR  = -1, /* Null pointer dereference */
	EALLOC    = -2, /* Dynamic allocation failure */
	EEMPTY    = -3, /* Unexpected empty input */
	EUNCLOSED = -4, /* Unclosed keyword placeholder */
	EMIXEDPH  = -5, /* Mixed positional and keyword placeholders */
	EBUFOV    = -6, /* Buffer overflow */
	EINVALID  = -7, /* Invalid or incomplete placeholder */
};

union query_item {
        int data_int;
        void* data_bytes;
};

struct query_part {
        void* pre;
        unsigned pre_len;
        union query_item item;
        int item_type;
        unsigned data_len;
        char format;
};

/* Returns size of input string minus any escaped percent signs */
int escaped_len(unsigned char* in,
		unsigned inlen);
/* Collapse %% into % */
int escape(unsigned char* out,
	   unsigned outlen,
	   unsigned char* in,
	   unsigned inlen);
/* Call escape() on a dynamically allocated buffer (result must be freed)*/
int escape_m(unsigned char** out,
	     unsigned* outlen,
	     unsigned char* in,
	     unsigned inlen);
/* Count the number of placeholders in a query and verify they are used 
   correctly */
int count_placeholders(unsigned char* in,
		       unsigned inlen);
/* Build an array of query_parts from placeholders in a query */
int search_placeholders(struct query_part* out,
			unsigned outlen,
			unsigned char* in,
			unsigned inlen);
/* Return a human-readable explanation of an error code from this module */
const char* placeholder_strerror(int err);
