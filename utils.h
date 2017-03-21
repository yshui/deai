#pragma once

#include <string.h>

#define tmalloc(type, nmem) (type *)calloc(nmem, sizeof(type))

static inline bool
di_type_check(unsigned int nargs, di_type_t *atypes,
		 unsigned int npars, di_type_t *ptypes) {
	if (nargs != npars)
		return false;

	return memcmp(atypes, ptypes, nargs*sizeof(di_type_t)) == 0;
}
