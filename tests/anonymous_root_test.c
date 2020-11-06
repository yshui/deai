#include <deai/deai.h>
#include <deai/helper.h>
#include <assert.h>

#include "common.h"

DEAI_PLUGIN_ENTRY_POINT(di) {
	auto object = di_new_object_with_type(struct di_object);
	auto roots = di_get_roots();
	DI_CHECK(roots);

	uint64_t root_handle = 0;
	di_callr(roots, "__add_anonymous", root_handle, object);
	auto weak = di_weakly_ref_object(object);
	di_unref_object(object);

	object = di_upgrade_weak_ref(weak);
	DI_CHECK(object != NULL);
	di_unref_object(object);

	di_call(roots, "__remove_anonymous", root_handle);
	object = di_upgrade_weak_ref(weak);
	DI_CHECK(object == NULL);

	di_drop_weak_ref(&weak);
	return 0;
}
