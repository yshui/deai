#include <deai/deai.h>
#include <deai/helper.h>
#include <assert.h>

#include "common.h"

DEAI_PLUGIN_ENTRY_POINT(di) {
	auto object = di_new_object_with_type(di_object);
	auto roots = di_get_roots();
	DI_CHECK(roots);

	bool added = false;
	di_callr(roots, "add_anonymous", added, object);
	DI_CHECK(added);
	auto weak = di_weakly_ref_object(object);
	di_unref_object(object);

	object = di_upgrade_weak_ref(weak);
	DI_CHECK(object != NULL);
	di_unref_object(object);

	bool removed = false;
	di_callr(roots, "remove_anonymous", removed, object);
	DI_CHECK(removed);
	object = di_upgrade_weak_ref(weak);
	DI_CHECK(object == NULL);

	di_drop_weak_ref(&weak);
	return 0;
}
