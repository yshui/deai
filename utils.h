#pragma once

#include <string.h>

#define tmalloc(type, nmem) (type *)calloc(nmem, sizeof(type))

static inline bool
piped_type_check(unsigned int nargs, piped_type_t *atypes,
		 unsigned int npars, piped_type_t *ptypes) {
	if (nargs != npars)
		return false;

	return memcmp(atypes, ptypes, nargs*sizeof(piped_type_t)) == 0;
}
