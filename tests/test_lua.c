#include <deai/deai.h>
#include <deai/helper.h>
#include <assert.h>

#include "common.h"

/// Convenient method to create an empty di_object
static struct di_object *create_di_object(struct di_object *unused _) {
	auto obj = di_new_object_with_type(struct di_object);
	di_set_type(obj, "deai.test:TestObject");
	return obj;
}

DEAI_PLUGIN_ENTRY_POINT(di) {
	di_remove_member_raw((struct di_object *)di, "lua");
	di_remove_member_raw((struct di_object *)di, "xorg");
	di_remove_member_raw((struct di_object *)di, "file");
	di_remove_member_raw((struct di_object *)di, "dbus");
	di_call(di, "load_plugin", (const char *)"./plugins/lua/di_lua.so");
	di_call(di, "load_plugin", (const char *)"./plugins/xorg/di_xorg.so");
	di_call(di, "load_plugin", (const char *)"./plugins/file/di_file.so");
	di_call(di, "load_plugin", (const char *)"./plugins/dbus/di_dbus.so");

	di_object_with_cleanup luam = NULL;
	DI_CHECK_OK(di_get(di, "lua", luam));

	di_method(di, "create_di_object", create_di_object);

	struct di_array dargv;
	di_get(di, "argv", dargv);

	const char **argv = dargv.arr;
	for (int i = 0; i < dargv.length; i++) {
		if (strcmp(argv[i], "--") == 0) {
			if (i + 1 < dargv.length) {
				di_call(luam, "load_script", argv[i + 1]);
			}
			break;
		}
	}
	di_free_array(dargv);
	return 0;
}
