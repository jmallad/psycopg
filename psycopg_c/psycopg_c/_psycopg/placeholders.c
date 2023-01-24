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

enum {
	PH_KWD = 1,
	PH_POS = 2
};

int find_placeholder(
	unsigned *out,
	unsigned *outlen,
	unsigned char* in,
	unsigned inlen,
	unsigned start)
{
	int end;
	unsigned p = start;
	if (!out || !outlen || !in) {
		/* Null pointer dereference */
		return -2;
	}
	while (p < inlen) {
		if (in[p] == '%') {
			if (p > inlen - 1) {
				break;
			}
			/* Check for escape */
			if (in[p+1] == '%') {
				p += 2;
				continue;
			}
			/* Check for keyword */
			if (in[p+1] == '(') {
				end = find_close(in, inlen, p+2);
				if (end < 0) {
					/* Unclosed keyword placeholder */
					return -1;
				}
				*out = p + 2;
				*outlen = (end - 1) - (p + 2);
				return PH_KWD;
			}
			/* Check for positional */
			if (in[p+1] == 's' ||
			    in[p+1] == 't' ||
			    in[p+1] == 'b') {
				*out = p + 1;
				*outlen = 1;
				p += 2;
				return PH_POS;
			}
		}
		p++;
	}
	return 0;
}

int count_placeholders(unsigned char* in,
		       unsigned inlen)
{
	int end;
	int ret;
        unsigned count = 0;
	unsigned ph = 0;
	unsigned phlen = 0;
	unsigned modes = 0;
	for (ret = 1;
	     ret > 0;
	     ret = find_placeholder(&ph, &phlen, in, inlen, ph + phlen)) {
		modes |= ret;
		count++;
	}
	if (ret < 0) {
		/* Malformed query */
		return -3;
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
