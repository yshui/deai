#pragma once

#include <deai.h>

int di_set(struct di_object *o, const char *prop, di_type_t type, void *val);
int di_get(struct di_object *o, const char *prop, di_type_t *type, void **val);

#define DI_SET(o, prop, v)                                                          \
	({                                                                          \
		__auto_type __tmp = (v);                                            \
		di_set(o, prop, di_typeof(__tmp), &__tmp);                          \
	})

#define DI_GET(o, prop, r)                                                          \
	({                                                                          \
		void *ret;                                                          \
		di_type_t rtype;                                                    \
		int rc;                                                             \
		do {                                                                \
			int rc = di_get(o, prop, &rtype, &ret);                     \
			if (rc != 0)                                                \
				break;                                              \
			if (di_typeof(r) != rtype) {                                \
				rc = -EINVAL;                                       \
				break;                                              \
			}                                                           \
			(r) = *(typeof(r)*)ret;                                     \
			free(ret);                                                  \
		} while (0);                                                        \
		rc;                                                                 \
	})

// TODO
// macro to generate c wrapper for di functions
