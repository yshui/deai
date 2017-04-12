#pragma once

#include <string.h>
#include <stdbool.h>

#define tmalloc(type, nmem) (type *)calloc(nmem, sizeof(type))
#define auto __auto_type

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

