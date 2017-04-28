#define _GNU_SOURCE
#include <deai.h>
#include <helper.h>

#include <stdio.h>

#include "utils.h"

PUBLIC int di_set(struct di_object *o, const char *prop, di_type_t type, void *val) {
	char *buf;
	void *ret;
	di_type_t rtype;
	asprintf(&buf, "__set_%s", prop);

	auto m = di_find_method(o, buf);
	free(buf);
	if (m) {
		const void *args[1] = {val};

		int cret = di_call_callable((void *)m, &rtype, &ret, 1, &type, args);
		free(ret);
		return cret;
	}

	m = di_find_method(o, "__set");
	if (m) {
		const void *args[2] = {&prop, val};
		di_type_t types[2] = {DI_TYPE_STRING, type};

		int cret = di_call_callable((void *)m, &rtype, &ret, 2, types, args);
		free(ret);
		return cret;
	}

	return -ENOENT;
}

PUBLIC int di_get(struct di_object *o, const char *prop, di_type_t *type, void **ret) {
	char *buf;
	asprintf(&buf, "__get_%s", prop);

	auto m = di_find_method(o, buf);
	free(buf);
	if (m) {
		int cret = di_call_callable_v((void *)m, type, ret, DI_LAST_TYPE);
		return cret;
	}

	m = di_find_method(o, "__get");
	if (m) {
		int cret = di_call_callable_v((void *)m, type, ret, 1, DI_TYPE_STRING, prop);
		return cret;
	}

	return -ENOENT;

}
