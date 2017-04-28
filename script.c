#include <ctype.h>
#include <deai.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include "log.h"
#include "utils.h"

struct di_value {
	di_type_t t;
	char buf[];
};

static struct di_value *error(const char *msg) {
	struct di_value *v = malloc(sizeof(struct di_value) + strlen(msg) + 1);
	v->t = DI_LAST_TYPE + 1;
	strcpy(v->buf, msg);
	return v;
}

static struct di_value *parse_number(const char *buf, const char **next_buf) {
	const char *nb = NULL;
	int pos = 0;
	while (isspace(buf[pos]))
		pos++;

	bool neg = false;
	if (buf[pos] == '-') {
		pos++;
		neg = true;
	} else if (buf[pos] == '+')
		pos++;

	int npos = pos;
	while (buf[npos] >= '0' && buf[npos] <= '9')
		npos++;

	if (npos == pos)
		return NULL;

	*next_buf = buf;
	if (buf[npos] == '.') {
		// We are parsing a float
		*next_buf = buf;
		return error("float");
	}

	di_type_t t;
	char *num = strndup(buf + pos, npos - pos);
	if (buf[npos] == 'u') {
		if (neg)
			return error("Integer overflow/underflow");
		npos++;
		if (buf[npos] == 'l') {
			npos++;
			t = DI_TYPE_UINT;
		} else
			t = DI_TYPE_NUINT;
	} else if (buf[npos] == 'l') {
		npos++;
		t = DI_TYPE_INT;
	} else {
		if (!isspace(buf[npos]) && buf[npos] != '\0') {
			return error("Malformed number");
		}
		t = DI_TYPE_NINT;
	}

	struct di_value *v = malloc(sizeof(struct di_value) + di_sizeof_type(t));
	v->t = t;
	errno = 0;
	if (t == DI_TYPE_NUINT || t == DI_TYPE_UINT) {
		unsigned long tmp = strtoul(num, NULL, 10);
		if (errno == ERANGE)
			return error("Integer overflow/underflow");
		if (t == DI_TYPE_NUINT) {
			if (tmp > UINT_MAX)
				return error("Integer overflow/underflow");
			*(unsigned int *)v->buf = (unsigned int)tmp;
		}
		else *(uint64_t*)v->buf = tmp;
	} else {
		long tmp = strtol(num, NULL, 10);
		if (errno == ERANGE)
			return error("Integer overflow/underflow");
		if (t == DI_TYPE_NINT) {
			if (tmp > INT_MAX)
				return error("Integer overflow/underflow");
			*(int *)v->buf = (int)tmp;
		}
		else *(int64_t*)v->buf = tmp;
	}
	free(num);

	*next_buf = buf + npos;
	return v;
}

static struct di_value *parse_string(const char *buf, const char **next_buf) {
	while (isspace(*buf))
		buf++;

	if (buf[0] != '"')
		return NULL;

	size_t len = 0;
	int pos = 1;
	while (buf[pos] != '"' && buf[pos] != '\0') {
		if (buf[pos] == '\\') {
			len++;
			if (buf[pos + 1] == '\0') {
				*next_buf = buf;
				return error("Un-terminated string");
			}
			pos += 2;
		} else {
			len++;
			pos++;
		}
	}

	if (buf[pos] == '\0') {
		*next_buf = buf;
		return error("Un-terminated string");
	}

	struct di_value *v = malloc(sizeof(struct di_value) + len + 1);
	v->t = DI_TYPE_STRING;
	pos = 1;
	len = 0;
	while (buf[pos] != '"') {
		if (buf[pos] == '\\') {
			switch (buf[pos + 1]) {
			case 'n': v->buf[len++] = '\n'; break;
			case 't': v->buf[len++] = '\t'; break;
			case 'b': v->buf[len++] = '\b'; break;
			case 'r': v->buf[len++] = '\r'; break;
			default: v->buf[len++] = buf[pos + 1]; break;
			}
			pos += 2;
		} else
			v->buf[len++] = buf[pos++];
	}
	v->buf[len] = '\0';
	*next_buf = buf+pos+1;
	return v;
}

static struct di_value *parse_value(const char *buf, const char **next_buf);

static struct di_value **parse_value_list(const char *buf, const char **next_buf) {
	struct di_value **arr = malloc(sizeof(void *)), *v;
	size_t arr_cap = 1;
	size_t arr_len = 0;
	while(true) {
		v = parse_value(buf, &buf);
		if (!v)
			break;

		if (arr_len >= arr_cap) {
			arr = realloc(arr, sizeof(void *)*arr_cap*2);
			arr_cap *= 2;
		}
		arr[arr_len++] = v;

		if (v->t > DI_LAST_TYPE)
			break;

		while(isblank(*buf))
			buf++;

		if (*buf == '\0' || *buf == '\r' || *buf == '\n')
			break;
		if (*buf != ',') {
			free(arr[arr_len-1]);
			arr[arr_len-1] = error("Missing delimiter");
			break;
		}
		buf++;
	}

	*next_buf = buf;
	arr = realloc(arr, sizeof(void *)*(arr_len+1));
	arr[arr_len] = NULL;
	return arr;
}

static struct di_value *parse_array(const char *buf, const char **next_buf) {
	while (isspace(*buf))
		buf++;

	int pos;
	if (*buf != '[')
		return NULL;
	auto v = parse_value_list(buf, next_buf);
	struct di_value *ret;
	if (v) {
		pos = 0;
		while(v[pos] != NULL) {
			if (v[pos]->t > DI_LAST_TYPE) {
				ret = v[pos];
				goto out;
			}
			pos++;
		}
	}
	auto nb = *next_buf;
	while (isspace(*nb))
		nb++;
	if (*nb != ']') {
		*next_buf = nb;
		ret = error("Un-terminated array");
		goto out;
	}

	if (!v) {
		ret = malloc(sizeof(struct di_value)+sizeof(struct di_array));
		ret->t = DI_TYPE_ARRAY;

		struct di_array *vv = (void *)ret->buf;
		vv->elem_type = DI_TYPE_VOID;
		vv->length = 0;
		vv->arr = NULL;
		goto out;
	}

	ret = malloc(sizeof(struct di_value)+sizeof(struct di_array));
	ret->t = DI_TYPE_ARRAY;

	struct di_array *vv = (void *)ret->buf;
	if (v[0]) {
		di_type_t t0 = v[0]->t;
		pos = 1;
		while(v[pos] != NULL) {
			if (v[pos]->t != t0) {
				*next_buf = buf;
				ret = error("Array of different types");
				goto out;
			}
			pos++;
		}

		size_t tsz = di_sizeof_type(t0);
		vv->elem_type = t0;
		vv->length = pos;
		vv->arr = malloc(tsz*pos);

		for (int i = 0; i < pos; i++)
			memcpy(vv->arr+i*tsz, v[i]->buf, tsz);
	} else {
		vv->elem_type = DI_TYPE_VOID;
		vv->length = 0;
		vv->arr = NULL;
	}

out:
	pos = 0;
	while(v && v[pos] != NULL) {
		if (v[pos] != ret)
			free(v[pos]);
		pos++;
	}
	free(v);
	return ret;
}

static struct di_value *parse_value(const char *buf, const char **next_buf) {
	auto v = parse_number(buf, next_buf);
	if (v != NULL)
		return v;

	v = parse_string(buf, next_buf);
	if (v != NULL)
		return v;

	v = parse_array(buf, next_buf);
	if (v != NULL)
		return v;

	*next_buf = buf;
	return error("Invalid value");
}

static char *parse_identifier(const char *buf, const char **next_buf) {
	if (!isalpha(*buf) && *buf != '_')
		return NULL;

	const char *endptr = buf;
	while(isalnum(*endptr) || *endptr == '_')
		endptr++;
	char *str = strndup(buf, endptr-buf);
	*next_buf = endptr;
	return str;
}

static int parse_call(struct deai *di, const char *buf, const char **next_buf) {
	while(isspace(*buf))
		buf++;

	struct di_object *log = (void *)di_find_module(di, "log");

	int ret = -1, i;
	char *mod = parse_identifier(buf, &buf);
	if (!mod) {
		if (*buf == '\0') {
			ret = 0;
			goto out;
		}
		di_log_va(log, DI_LOG_ERROR, "Invalid module name\n");
		goto out;
	}

	char *method;
	if (*buf == '.') {
		buf++;
		method = parse_identifier(buf, &buf);

		if (!method) {
			di_log_va(log, DI_LOG_ERROR, "Invalid method name\n");
			free(mod);
			goto out;
		}
	} else {
		method = mod;
		mod = NULL;
	}

	auto vl = parse_value_list(buf, &buf);
	int nargs;
	for (nargs = 0; vl[nargs]; nargs++)
		if (vl[nargs]->t > DI_LAST_TYPE) {
			di_log_va(log, DI_LOG_ERROR, "Failed to parse call: %s %s\n", vl[nargs]->buf, buf);
			goto out2;
		}

	void **args = NULL;
	di_type_t *atypes = NULL;
	if (nargs > 0) {
		args = calloc(nargs, sizeof(void *));
		atypes = calloc(nargs, sizeof(di_type_t));
		for (i = 0; i < nargs; i++) {
			args[i] = calloc(1, sizeof(void *));
			*((void **)(args[i])) = vl[i]->buf;
			atypes[i] = vl[i]->t;
		}
	}

	di_type_t rtype;
	void *retd;

	ret = 1;
	struct di_module *m;
	if (mod) {
		m = di_find_module(di, mod);
		if (!m) {
			di_log_va(log, DI_LOG_ERROR, "Module %s not found\n", mod);
			goto out3;
		}
	} else {
		m = (void *)di;
		di_ref_object((void *)m);
	}

	struct di_method *mt = di_find_method((void *)m, method);
	if (!mt) {
		di_log_va(log, DI_LOG_ERROR, "Method %s not found in module %s\n", method, mod);
		goto out4;
	}

	di_call_callable((void *)mt, &rtype, &retd, nargs, atypes, (const void * const *)args);
	if (rtype == DI_TYPE_OBJECT && *(void **)retd != NULL)
		di_unref_object(*(void **)retd);
	free(retd);
out4:
	di_unref_object((void *)m);
out3:
	for (i = 0; i < nargs; i++)
		free(args[i]);
	free(args);
	free(atypes);
out2:
	free(method);
	free(mod);
	for (i = 0; vl[i]; i++)
		free(vl[i]);
	free(vl);
out:
	*next_buf = buf;
	di_unref_object((void *)log);
	return ret;
}

void parse_script(struct deai *di, const char *buf) {
	while(parse_call(di, buf, &buf) > 0);
}
