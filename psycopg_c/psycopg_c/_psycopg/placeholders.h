#pragma once

enum {
    ITEM_INT = 0,
    ITEM_STR = 1
};

enum {
	PH_KWD = 1,
	PH_POS = 2
};

enum {
	ENULLPTR = -1,
	EALLOC   = -2,
	EEMPTY   = -3,
	EUNCLOSED = -4,
	EMIXEDPH  = -5
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

int count_placeholders(unsigned char* in,
		       unsigned inlen);
int search_placeholders(struct query_part* out,
			unsigned outlen,
			unsigned char* in,
			unsigned inlen);
const char* placeholder_strerror(int err);
