#include <deai/module.h>

#include "di_internal.h"

PUBLIC_DEAI_API struct deai *di_module_get_deai(struct di_module *m) {
	return m->di;
}
