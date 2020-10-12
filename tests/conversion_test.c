#include <deai/deai.h>
#include <deai/helper.h>
#include <assert.h>

#include "common.h"
#include "utils.h"

static void takes_string(const char *str unused) {
}

static const char *takes_string_and_return(const char *str) {
	// We only borrows str, so to return an owned value, we need to copy it
	return strdup(str);
}

DEAI_PLUGIN_ENTRY_POINT(di) {
	di_closure_with_cleanup test1 = di_closure(takes_string, (), const char *const);
	di_closure_with_cleanup test2 = di_closure(takes_string_and_return, (), const char *);

	di_type_t retty;
	union di_value val, val2;
	// Test borrowed string -> string_literal
	DI_CHECK_OK(di_call_object((struct di_object *)test1, &retty, &val,
	                           DI_TYPE_STRING, "a string", DI_LAST_TYPE));

	// Test owned string literal -> string
	bool cloned;
	val.string_literal = "test";
	di_type_conversion(DI_TYPE_STRING_LITERAL, &val, DI_TYPE_STRING, &val2, false, &cloned);
	DI_CHECK(cloned);
	free((char *)val2.string);

	// Test borrowed string literal -> string
	DI_CHECK_OK(di_call_object((struct di_object *)test2, &retty, &val,
	                           DI_TYPE_STRING_LITERAL, "string literal", DI_LAST_TYPE));
	DI_CHECK(retty == DI_TYPE_STRING);
	free((char *)val.string);
	return 0;
}
