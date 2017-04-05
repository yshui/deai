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
#include "event.h"
#include "utils.h"
#include "uthash.h"
#include "script.h"
#include "log.h"

void load_plugin(struct deai *p, int fd, const char *fname) {
	void *handle;
	char *buf = malloc(strlen(fname)+3);
	buf[0] = '.';
	buf[1] = '/';
	strcpy(buf+2, fname);

	fchdir(fd);
	handle = dlopen(buf, RTLD_NOW);
	free(buf);

	if (!handle) {
		fprintf(stderr, "Failed to load %s: %s", fname, dlerror());
		return;
	}

	init_fn_t init_fn = dlsym(handle, "di_plugin_init");
	if (!init_fn) {
		fprintf(stderr, "%s doesn't have a di_plugin_init function", fname);
		return;
	}

	init_fn(p);
}

int main(int argc, const char * const *argv) {
	struct deai *p = tmalloc(struct deai, 1);
	p->m = NULL;
	p->loop = EV_DEFAULT;

	// Register deai signals
	di_register_signal((void *)p, "new-module", 1, DI_TYPE_STRING);
	di_register_signal((void *)p, "startup", 0);

	// (1) Initialize builtin modules first
	di_init_event_module(p);
	di_init_log(p, 10);

	const char *plugin_dir = "./";
	if (argc > 3) {
		printf("Usage: %s [plugin dir] [config]\n", argv[0]);
		exit(1);
	} else if (argc >= 2)
		plugin_dir = argv[1];

	// (2) Load external plugins
	int dirfd = open(plugin_dir, O_DIRECTORY|O_RDONLY);
	DIR *dir = fdopendir(dirfd);
	struct dirent *dent;
	while((dent = readdir(dir))) {
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

	// (3) Signal startup finish
	di_emit_signal((void *)p, "startup", NULL);

	// (4) Load config script
	if (argc >= 3) {
		int fd = open(argv[2], O_RDONLY);
		if (fd < 0)
			goto run;

		struct stat buf;
		int ret = fstat(fd, &buf);
		if (ret != 0)
			goto run;

		char *sbuf = malloc(buf.st_size+1);
		if (!sbuf)
			goto run;
		read(fd, sbuf, buf.st_size);
		sbuf[buf.st_size] = '\0';
		parse_script(p, sbuf);
	}

run:
	// (4) Start mainloop
	ev_run(p->loop, 0);

	// (5) Exit
	struct di_module_internal *pm = p->m;
	while(pm) {
		auto next_pm = pm->hh.next;
		HASH_DEL(p->m, pm);
		di_free_object((void *)pm);
		pm = next_pm;
	}
	di_free_object((void *)p);
}
