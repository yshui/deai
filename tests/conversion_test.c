#include <deai/deai.h>
#include <deai/helper.h>
#include <assert.h>

#include "common.h"

static void takes_string(void *obj unused, char *str unused) {
}

PUBLIC int di_plugin_init(struct deai *di) {
	DI_CHECK_OK(di_method(di, "takes_string", takes_string, char *));

	di_type_t retty;
	void *retval;
	di_callx((struct di_object *)di, "takes_string", &retty, &retval,
	         DI_TYPE_STRING_LITERAL, "a string", DI_LAST_TYPE);
	return 0;
}
