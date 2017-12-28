#include <assert.h>
#include <deai/deai.h>
#include <deai/helper.h>

PUBLIC int di_plugin_init(struct deai *di) {
	di_call(di, "load_plugin", (char *)"./plugins/lua/di_lua.so");
	di_getmi(di, lua);
	assert(luam);

	struct di_array dargv;
	di_get(di, "argv", dargv);

	const char **argv = dargv.arr;
	for (int i = 0; i < dargv.length; i++) {
		if (strcmp(argv[i], "--") == 0) {
			if (i+1 < dargv.length)
				di_call(luam, "load_script", argv[i+1]);
			break;
		}
	}
	return 0;
}
