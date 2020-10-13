#include <deai/deai.h>
#include <deai/helper.h>
#include <assert.h>

#include "common.h"

DEAI_PLUGIN_ENTRY_POINT(di) {
	auto object = di_new_object_with_type(struct di_object);
	auto handler = di_new_object_with_type(struct di_object);

	auto listen_handle = di_listen_to(object, di_string_borrow("some_event"), handler);

	di_unref_object(handler);
	di_unref_object(object);
	di_unref_object(listen_handle);
	return 0;
}
