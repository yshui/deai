#include <deai/deai.h>
#include <deai/helper.h>
#include <assert.h>

#include "common.h"
#include "utils.h"

static void takes_string(const char *str unused) {
}

static di_string takes_string_and_return(di_string str) {
	// We only borrows str, so to return an owned value, we need to copy it
	return di_clone_string(str);
}

DEAI_PLUGIN_ENTRY_POINT(di) {
	scoped_di_closure *test1 = di_closure(takes_string, (), const char *);
	scoped_di_closure *test2 = di_closure(takes_string_and_return, (), di_string);

	di_type retty;
	di_value val, val2;
	// Test string_literal -> string_literal
	DI_CHECK_OK(di_call_object((di_object *)test1, &retty, &val,
	                           DI_TYPE_STRING_LITERAL, "a string", DI_LAST_TYPE));

	// Test owned string literal -> string
	val.string_literal = "test";
	di_type_conversion(DI_TYPE_STRING_LITERAL, &val, DI_TYPE_STRING, &val2, false);
	di_free_string(val2.string);

	// Test borrowed string literal -> string
	DI_CHECK_OK(di_call_object((di_object *)test2, &retty, &val,
	                           DI_TYPE_STRING_LITERAL, "string literal", DI_LAST_TYPE));
	DI_CHECK(retty == DI_TYPE_STRING);
	di_free_string(val.string);
	return 0;
}
