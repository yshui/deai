#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <fcntl.h>
#include <ev.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <unistd.h>

#include <plugin.h>

#include "di_internal.h"
#include "utils.h"
#include "uthash.h"

const struct di_event_desc di_ev_new_module = {
	.name = "new-module",
	.nargs = 1,
	.types = (di_type_t[1]){ DI_TYPE_STRING },
};
const struct di_event_desc di_ev_new_fn = {
	.name = "new-function",
	.nargs = 1,
	.types = (di_type_t[1]){ DI_TYPE_STRING },
};
const struct di_event_desc di_ev_startup = {
	.name = "startup",
	.nargs = 0,
	.types = NULL,
};

void load_plugin(struct deai *p, int fd, const char *fname) {
	void *handle;
	int dirsave = open(".", O_RDONLY|O_DIRECTORY);

	fchdir(fd);
	handle = dlopen(fname, RTLD_NOW);

	if (!handle) {
		fprintf(stderr, "Failed to load %s: %s", fname, dlerror());
		return;
	}

	init_fn_t init_fn = dlsym(handle, "di_plugin_init");
	if (!init_fn) {
		fprintf(stderr, "%s doesn't have a di_plugin_init function");
		return;
	}

	struct di_module *pm = tmalloc(struct di_module, 1);
	pm->di = p;
	init_fn(pm);
}

int main(int argc, const char * const *argv) {
	struct deai *p = tmalloc(struct deai, 1);
	p->m = NULL;
	p->loop = EV_DEFAULT;

	const char *plugin_dir = "./";
	if (argc > 2) {
		printf("Usage: %s [plugin dir]\n", argv[0]);
		exit(1);
	} else if (argc == 2)
		plugin_dir = argv[1];

	int dirfd = open(plugin_dir, O_DIRECTORY|O_RDONLY);
	DIR *dir = fdopendir(dirfd);
	struct dirent *dent;
	while(dent = readdir(dir)) {
		bool is_reg = dent->d_type == DT_REG;
		if (dent->d_type == DT_UNKNOWN || dent->d_type == DT_LNK) {
			struct stat buf;
			int ret = fstatat(dirfd, dent->d_name, &buf, 0);
			if (ret != 0) {
				perror("stat");
				exit(1);
			}

			is_reg = S_ISREG(buf.st_mode);
		}

		if (!is_reg)
			continue;

		int nlen = strlen(dent->d_name);
		if (nlen >= 3 && strcmp(dent->d_name+nlen-3, ".so") == 0)
			load_plugin(p, dirfd, dent->d_name);
	}
	closedir(dir);

	di_event_source_emit(&p->core_ev, &di_ev_startup, NULL);
	ev_run(p->loop, 0);
}
