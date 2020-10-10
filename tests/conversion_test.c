#include <deai/deai.h>
#include <deai/helper.h>
#include <assert.h>

#include "common.h"

static void takes_string(void *obj unused, char *str unused) {
}

static void takes_string_and_modify(void *obj unused, char *str) {
	// Make sure there is space for write
	DI_CHECK(strlen(str) >= 1);
	*str = '\0';
}

DEAI_PLUGIN_ENTRY_POINT(di) {
	DI_CHECK_OK(di_method(di, "takes_string", takes_string, char *));
	DI_CHECK_OK(di_method(di, "takes_string_and_modify", takes_string_and_modify, char *));

	di_type_t retty;
	union di_value retval;
	bool called;
	di_callx((struct di_object *)di, "takes_string", &retty, &retval,
	         di_tuple((const char *)"a string"), &called);

	di_call(di, "takes_string_and_modify", (const char *)"string literal");
	return 0;
}
