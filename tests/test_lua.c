#include <deai/deai.h>
#include <deai/helper.h>
#include <assert.h>

#include "common.h"

PUBLIC int di_plugin_init(struct deai *di) {
	di_remove_member((struct di_object *)di, "lua");
	di_remove_member((struct di_object *)di, "xorg");
	di_remove_member((struct di_object *)di, "file");
	di_remove_member((struct di_object *)di, "dbus");
	di_call(di, "load_plugin", (char *)"./plugins/lua/di_lua.so");
	di_call(di, "load_plugin", (char *)"./plugins/xorg/di_xorg.so");
	di_call(di, "load_plugin", (char *)"./plugins/file/di_file.so");
	di_call(di, "load_plugin", (char *)"./plugins/dbus/di_dbus.so");
	di_getmi(di, lua);
	assert(luam);

	struct di_array dargv;
	di_get(di, "argv", dargv);

	const char **argv = dargv.arr;
	for (int i = 0; i < dargv.length; i++) {
		if (strcmp(argv[i], "--") == 0) {
			if (i + 1 < dargv.length)
				di_call(luam, "load_script", argv[i + 1]);
			break;
		}
	}
	di_free_value(DI_TYPE_ARRAY, &dargv);
	return 0;
}
