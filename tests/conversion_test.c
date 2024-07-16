#include <deai/deai.h>
#include <deai/helper.h>
#include <deai/type.h>

#include "common.h"

static void takes_string(const char *str unused) {
}

static di_string takes_string_and_return(di_string str) {
	// We only borrows str, so to return an owned value, we need to copy it
	return di_clone_string(str);
}

DEAI_PLUGIN_ENTRY_POINT(di) {
	scoped_di_closure *test1 = di_make_closure(takes_string, (), const char *);
	scoped_di_closure *test2 = di_make_closure(takes_string_and_return, (), di_string);

	di_type retty;
	di_value val, val2;
	const char *str_literal = "a string";
	di_tuple args = {
	    .length = 1,
	    .elements =
	        (di_variant[]){
	            {.type = DI_TYPE_STRING_LITERAL, .value = (di_value *)&str_literal},
	        },
	};
	// Test string_literal -> string_literal
	DI_CHECK_OK(di_call_object((di_object *)test1, &retty, &val, args));

	// Test owned string literal -> string
	val.string_literal = "test";
	di_type_conversion(DI_TYPE_STRING_LITERAL, &val, DI_TYPE_STRING, &val2, false);
	di_free_string(val2.string);

	// Test borrowed string literal -> string
	DI_CHECK_OK(di_call_object((di_object *)test2, &retty, &val, args));
	DI_CHECK(retty == DI_TYPE_STRING);
	DI_CHECK(strncmp(val.string.data, str_literal, val.string.length) == 0);
	di_free_string(val.string);
}
