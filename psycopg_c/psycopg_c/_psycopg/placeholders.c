enum item_type_enum {
    ITEM_INT = 0,
    ITEM_STR = 1
};

union query_item {
        int data_int;
        void* data_bytes;
};

struct query_part {
        void* pre;
        unsigned pre_len;
        union query_item item;
        enum item_type_enum item_type;
        unsigned data_len;
        char format;
};

static int find_close(unsigned char* query,
		      unsigned inlen,
		      unsigned start)
{
	unsigned p;
	
	for (p = start; p < inlen; p++) {
		if (query[p] == ')') {
			return p;
		}
	}
	
	return -1;
}

int count_placeholders(unsigned char* query,
		       unsigned inlen)
{
	int end;
	unsigned p = 0;
        unsigned count = 0;
	unsigned modes = 0; /* bit 0 for positional, bit 1 for keyword */
	while (p < inlen) {
		if (query[p] == '%') {
			if (p > inlen - 1) {
				break;
			}
			/* Check for escape */
			if (query[p+1] == '%') {
				p += 2;
				continue;
			}
                        /* Check for keyword */
			if (query[p+1] == '(') {
				end = find_close(query, inlen, p+2);
				if (end < 0) {
					/* Unclosed keyword placeholder */
					return -1;
				}
				p = end + 1;
				modes |= 1;
				count++;
				continue;
			}
			/* Check for positional */
			if (query[p+1] == 's' ||
			    query[p+1] == 't' ||
			    query[p+1] == 'b') {
				p += 2;
				modes |= 2;
				count++;
				continue;
			}
		}
		p++;
	}
	
	if (modes == 3) {
		/* Mixed keyword and positional placeholders */
		return -2;
	}
	
	return count;	
}

#if (PLACEHOLDER_TEST)
/**** testing code ****/

#include <string.h>
#include <stdio.h>

int main(int argc,
	 char** argv)
{
	unsigned char* query = (unsigned char*)"select %s from %s %% %b";
	unsigned inlen = strlen((char*)query);
	int ret = count_placeholders(query, inlen);
	fprintf(stderr, "query: %s\nplaceholders: %d\n", query, ret);
	query = (unsigned char*)"%(k1) %% and %% %(k2) where %(k3)";
	inlen = strlen((char*)query);
	ret = count_placeholders(query, inlen);
	fprintf(stderr, "query: %s\nplaceholders: %d\n", query, ret);
	query = (unsigned char*)"mixed %(keyword) and %s positional %b";
	inlen = strlen((char*)query);
	ret = count_placeholders(query, inlen);
	fprintf(stderr, "query: %s\nplaceholders: %d\n", query, ret);	
}

#endif
