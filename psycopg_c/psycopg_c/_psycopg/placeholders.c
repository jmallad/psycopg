#include <string.h>
#include <stdlib.h>
#include "placeholders.h"

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

static int find_placeholder(
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
		return ENULLPTR;
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
					return EUNCLOSED;
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
			/* Invalid/incomplete placeholder */
			return EINVALID;
		}
		p++;
	}
	return 0;
}

/* Returns size of input string minus any escaped percent signs */
int escaped_len(unsigned char* in,
		unsigned inlen)
{
	unsigned ip;
	unsigned op;
	unsigned count = 0;
	if (!in) {
		return ENULLPTR;
	}
	if (!inlen) {
		return EEMPTY;
	}
	/* Count the number of escapes */
	for (ip = 0; ip < inlen; ip++) {
		if (in[ip] == '%') {
			if (ip > inlen - 1) {
				break;
			}
			/* Check for escape */
			if (in[ip+1] == '%') {
				ip += 2;
				count++;
				continue;
			}
		}
	}
	return inlen - count;
}

/* Collapse %% into % */
int escape(unsigned char* out,
	   unsigned outlen,
	   unsigned char* in,
	   unsigned inlen)
{
	unsigned ip;
	unsigned op;
	if (!out || !in) {
		return ENULLPTR;
	}
	if (!outlen || !inlen) {
		return EEMPTY;
	}
	/* Write escaped output */
	op = 0;
	ip = 0;
	while (ip < inlen && op < outlen) {
		if (in[ip] == '%') {
			if (ip > inlen - 1) {
				break;
			}
			/* Check for escape */
			if (in[ip+1] == '%') {
				ip++;
				continue;
			}
		}
		out[op] = in[ip];
		op++;
		ip++;
	}
	return SUCCESS;	
}

/* Call escape() on a dynamically allocated buffer */
int escape_m(unsigned char** out,
	     unsigned* outlen,
	     unsigned char* in,
	     unsigned inlen)
{
	if (!out || !in || !outlen) {
		return ENULLPTR;
	}
	if (!inlen) {
		return EEMPTY;
	}
	*outlen = escaped_len(in, inlen);
	*out = malloc(*outlen);
	if (!(*out)) {
		return EALLOC;
	}
	return escape(*out, *outlen, in, inlen);
}

int count_placeholders(unsigned char* in,
		       unsigned inlen)
{
	int ret;
        unsigned count = 0;
	unsigned ph = 0;
	unsigned phlen = 0;
	unsigned modes = 0;
	
	while (1) {
		ret = find_placeholder(&ph, &phlen, in, inlen, ph + phlen);
		if (ret <= 0) {
			break;
		}
		modes |= ret;
		count++;
	}
	if (ret < 0) {
		/* Malformed query */
		return ret;
	}
	if (modes == 3) {
		/* Mixed keyword and positional placeholders */
		return EMIXEDPH;
	}
	
	return count;	
}

int search_placeholders(struct query_part* out,
			unsigned outlen,
			unsigned char* in,
			unsigned inlen)
{
	int ret;
	unsigned count = 0;
	unsigned ph = 0;
	unsigned phlen = 0;

	if (!out || !in) {
		return ENULLPTR;
	}
	if (!outlen || !inlen) {
		return EEMPTY;
	}
	while (1) {
		ret = find_placeholder(&ph, &phlen, in, inlen, ph + phlen);
		if (ret <= 0) {
			break;
		}
		if (count > outlen) {
			return EBUFOV;
		}
		out[count].pre = &in[ph];
		out[count].pre_len = phlen;
		if (ret == PH_POS) {
			/* We already verified this is in the valid set of
			   inputs in previous call to count_placeholders */
			out[count].format = in[ph];
			out[count].item.data_int = count;
		}
		else {
			/* Auto format for keyword arguments */
			out[count].format = 's';
			out[count].item.data_bytes = &in[ph];
		}
		count++;
		
	}
	return SUCCESS;
}

const char* placeholder_strerror(int err)
{
	switch (err){
	case ENULLPTR:
		return "Null pointer dereference";
	case EALLOC:
		return "Dynamic allocation failure";
	case EEMPTY:
		return "Unexpected empty buffer";
	case EUNCLOSED:
		return "Unclosed keyword placeholder";
	case EMIXEDPH:
		return "Mixed usage of keyword and positional placeholders";
	case EINVALID:
		return "Invalid or incomplete placeholder";
	}
	return "Unrecognized return code";
}

#if (PLACEHOLDER_TEST)
/**** testing code ****/

#include <string.h>
#include <stdio.h>

int main(int argc,
	 char** argv)
{
	unsigned char* escaped;
	unsigned escapelen;
	unsigned esclen;
	unsigned char* query = (unsigned char*)"select %s from %s %% %b";
	unsigned inlen = strlen((char*)query);
	int ret = count_placeholders(query, inlen);
	fprintf(stderr, "query: %s\nplaceholders: %d\n", query, ret);
	ret = escape_m(&escaped, &escapelen, query, inlen);
	fprintf(stderr, "escaped: %s\n\n", escaped);
	free(escaped);
	
	query = (unsigned char*)"%(k1) %% and %% %(k2) where %(k3)";
	inlen = strlen((char*)query);
	ret = count_placeholders(query, inlen);	
	fprintf(stderr, "query: %s\nplaceholders: %d\n", query, ret);
	ret = escape_m(&escaped, &escapelen, query, inlen);
	fprintf(stderr, "escaped: %s\n\n", escaped);
	free(escaped);
	
	query = (unsigned char*)"mixed %(keyword) and %s positional %b";
	inlen = strlen((char*)query);
	ret = count_placeholders(query, inlen);
	fprintf(stderr, "query: %s\nplaceholders: %d\n", query, ret);
	ret = escape_m(&escaped, &escapelen, query, inlen);
	fprintf(stderr, "escaped: %s\n\n", escaped);
	free(escaped);
}

#endif
