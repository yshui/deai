#include <deai/module.h>

#include "di_internal.h"

struct deai *di_module_get_deai(struct di_module *m) {
	return m->di;
}
