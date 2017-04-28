#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <ev.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <deai.h>

#include "di_internal.h"
#include "event_internal.h"
#include "log_internal.h"
#include "script.h"
#include "uthash.h"
#include "utils.h"

static void load_plugin(struct deai *p, const char *sopath) {
	void *handle = dlopen(sopath, RTLD_NOW);

	if (!handle) {
		fprintf(stderr, "Failed to load %s: %s\n", sopath, dlerror());
		return;
	}

	init_fn_t init_fn = dlsym(handle, "di_plugin_init");
	if (!init_fn) {
		fprintf(stderr, "%s doesn't have a di_plugin_init function\n", sopath);
		return;
	}

	init_fn(p);
}

static int load_plugin_dir(struct deai *di, const char *path) {
	// (2) Load external plugins
	char rpath[PATH_MAX];
	realpath(path, rpath);

	int dirfd = open(path, O_DIRECTORY | O_RDONLY);
	DIR *dir = fdopendir(dirfd);
	struct dirent *dent;
	while ((dent = readdir(dir))) {
		bool is_reg = dent->d_type == DT_REG;
		if (dent->d_type == DT_UNKNOWN || dent->d_type == DT_LNK) {
			struct stat buf;
			int ret = fstatat(dirfd, dent->d_name, &buf, 0);
			if (ret != 0) {
				perror("stat");
				return -1;
			}

			is_reg = S_ISREG(buf.st_mode);
		}

		if (!is_reg)
			continue;

		int nlen = strlen(dent->d_name);
		if (nlen >= 3 && strcmp(dent->d_name + nlen - 3, ".so") == 0) {
			char *sopath;
			asprintf(&sopath, "%s/%s", rpath, dent->d_name);
			load_plugin(di, sopath);
			free(sopath);
		}
	}
	closedir(dir);
	return 0;
}

int di_chdir(struct di_object *p, const char *dir) {
	int ret = chdir(dir);
	if (ret != 0)
		ret = -errno;

	errno = 0;
	return ret;
}

// TODO:
// deai:
//  setenv, getenv
// log:
//  set_log_level, set_log_target
int main(int argc, const char *const *argv) {
	struct deai *p = di_new_object_with_type(struct deai);
	struct di_module_internal *pm;
	p->m = NULL;
	p->loop = EV_DEFAULT;

	// Register deai signals
	di_register_signal((void *)p, "new-module", 1, DI_TYPE_STRING);
	di_register_signal((void *)p, "startup", 0);

	// (1) Initialize builtin modules first
	di_init_event_module(p);
	di_init_log(p, 10);

	if (argc != 2) {
		printf("Usage: %s <startup.di>\n", argv[0]);
		exit(1);
	}

	auto tm = di_create_typed_method((di_fn_t)load_plugin_dir, "load_plugin",
	                                 DI_TYPE_NINT, 1, DI_TYPE_STRING);
	di_register_typed_method((void *)p, tm);

	tm = di_create_typed_method((di_fn_t)di_chdir, "chdir",
	                                 DI_TYPE_NINT, 1, DI_TYPE_STRING);
	di_register_typed_method((void *)p, tm);

	// (2) Load startup script
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open startup script: %s\n",
		        strerror(errno));
		goto out;
	}

	struct stat buf;
	int ret = fstat(fd, &buf);
	if (ret != 0)
		goto run;

	char *sbuf = malloc(buf.st_size + 1);
	if (!sbuf)
		goto run;
	read(fd, sbuf, buf.st_size);
	sbuf[buf.st_size] = '\0';
	parse_script(p, sbuf);
	free(sbuf);

run:
	// (4) Start mainloop
	ev_run(p->loop, 0);

out:
	// (5) Exit
	pm = p->m;
	while (pm) {
		auto next_pm = pm->hh.next;
		HASH_DEL(p->m, pm);
		// printf("%s:%d\n",pm->name, pm->ref_count);
		di_unref_object((void *)pm);
		pm = next_pm;
	}
	di_unref_object((void *)p);
}
