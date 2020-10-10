#include <deai/deai.h>
#include <deai/helper.h>
#include <assert.h>

#include "common.h"

DEAI_PLUGIN_ENTRY_POINT(di) {
	auto object = di_new_object_with_type(struct di_object);
	void *root_handle;
	di_callr(di, "add_anonymous_root", root_handle, object);
	auto weak = di_weakly_ref_object(object);
	di_unref_object(object);

	object = di_upgrade_weak_ref(weak);
	DI_CHECK(object != NULL);
	di_unref_object(object);

	di_call(di, "remove_anonymous_root", root_handle);
	object = di_upgrade_weak_ref(weak);
	DI_CHECK(object == NULL);

	di_drop_weak_ref(&weak);
	return 0;
}
