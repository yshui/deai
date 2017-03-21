#pragma once

#include <string.h>

#define tmalloc(type, nmem) (type *)calloc(nmem, sizeof(type))
#define auto __auto_type
#define cleanup(func) __attribute__((cleanup(func)))

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member)                                             \
	__extension__({                                                             \
		const typeof(((type *)0)->member) *__mptr = (ptr);                  \
		(type *)((char *)__mptr - offsetof(type, member));                  \
	})

#define trivial_cleanup_fn(type, func)                                              \
	static inline void func(type *p) {                                          \
		if (*p)                                                             \
			free((void *)*p);                                           \
	}                                                                           \
	struct __useless_struct_to_allow_trailing_semicolon__

#define PUBLIC __attribute__((visibility("default")))

static inline bool di_type_check(unsigned int nargs, di_type_t *atypes,
                                 unsigned int npars, di_type_t *ptypes) {
	if (nargs != npars)
		return false;

	return memcmp(atypes, ptypes, nargs * sizeof(di_type_t)) == 0;
}
