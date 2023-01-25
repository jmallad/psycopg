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

/* Collapse %% into % */
int escape(unsigned char** out,
	   unsigned* outlen,
	   unsigned char* in,
	   unsigned inlen);
int count_placeholders(unsigned char* in,
		       unsigned inlen);
int search_placeholders(struct query_part* out,
			unsigned outlen,
			unsigned char* in,
			unsigned inlen);
const char* placeholder_strerror(int err);
