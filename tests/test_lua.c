#include <deai/deai.h>
#include <deai/helper.h>
#include <assert.h>

#include "common.h"

/// Convenient method to create an empty di_object
static di_object *create_di_object(di_object *unused _) {
	auto obj = di_new_object_with_type(di_object);
	di_set_type(obj, "deai.test:TestObject");
	return obj;
}

DEAI_PLUGIN_ENTRY_POINT(di) {
	di_delete_member_raw((di_object *)di, di_string_borrow("lua"));
	di_delete_member_raw((di_object *)di, di_string_borrow("xorg"));
	di_delete_member_raw((di_object *)di, di_string_borrow("file"));
	di_delete_member_raw((di_object *)di, di_string_borrow("dbus"));
	di_call(di, "load_plugin", (const char *)"./plugins/lua/di_lua.so");
	di_call(di, "load_plugin", (const char *)"./plugins/xorg/di_xorg.so");
	di_call(di, "load_plugin", (const char *)"./plugins/file/di_file.so");
	di_call(di, "load_plugin", (const char *)"./plugins/dbus/di_dbus.so");
	di_call(di, "load_plugin", (const char *)"./plugins/hwinfo/di_hwinfo.so");

	scoped_di_object *luam = NULL;
	DI_CHECK_OK(di_get(di, "lua", luam));

	di_method(di, "create_di_object", create_di_object);

	di_array dargv;
	di_get(di, "argv", dargv);

	const char **argv = dargv.arr;
	for (int i = 0; i < dargv.length; i++) {
		if (strcmp(argv[i], "--") == 0) {
			if (i + 1 < dargv.length) {
				scoped_di_object *o = NULL;
				DI_CHECK_OK(di_callr(luam, "load_script", o, di_string_borrow(argv[i + 1])));

				di_string errmsg;
				if (di_get(o, "errmsg", errmsg) == 0) {
					fprintf(stderr, "Failed to load script %.*s\n",
					        (int)errmsg.length, errmsg.data);
					DI_PANIC();
				}
			}
			break;
		}
	}
	di_free_array(dargv);
	return 0;
}
