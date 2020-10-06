/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include <deai/builtin/log.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include "di_internal.h"
#include "log.h"
#include "utils.h"

struct di_log {
	struct di_module;

	int log_level;
};

static int level_lookup(char *l) {
	if (strcmp(l, "error") == 0)
		return DI_LOG_ERROR;
	if (strcmp(l, "warn") == 0)
		return DI_LOG_WARN;
	if (strcmp(l, "info") == 0)
		return DI_LOG_INFO;
	if (strcmp(l, "debug") == 0)
		return DI_LOG_DEBUG;

	return DI_LOG_DEBUG + 1;
}

static const char *level_tostring(int log_level) {
	switch (log_level) {
	case DI_LOG_DEBUG: return "debug";
	case DI_LOG_INFO: return "info";
	case DI_LOG_WARN: return "warn";
	case DI_LOG_ERROR: return "error";
	default: return NULL;
	}
}

// Function exposed via di_object to be used by any plugins
static int di_log(struct di_object *o, di_type_t *rt, void **ret, struct di_tuple t) {
	if (t.length != 2)
		return -EINVAL;
	if (t.elem_type[0] != DI_TYPE_STRING || t.elem_type[1] != DI_TYPE_STRING)
		return -EINVAL;

	char *log_level = *(char **)t.tuple[0];
	char *str = *(char **)t.tuple[1];
	if (!str)
		str = "(nil)";
	*rt = DI_TYPE_NINT;
	int *res = *ret = malloc(di_sizeof_type(DI_TYPE_NINT));

	struct di_log *l = (void *)o;
	if (level_lookup(log_level) > l->log_level)
		return 0;

	with_object_cleanup(di_object) ltgt = NULL;
	if (di_get(l, "log_target", ltgt) != 0) {
		*res = 0;
		return 0;
	} else {
		int wrc = 0;
		int rc = di_callr(ltgt, "write", wrc, str);
		if (rc != 0)
			*res = rc;
		else
			*res = wrc;
	}
	return 0;
}

struct log_file {
	struct di_object_internal;
	FILE *f;
};

static int _stderr_write(struct di_object *t, char *log) {
	return fputs(log, stderr);
}

static int file_target_write(struct log_file *lf, char *log) {
	auto rc = fputs(log, lf->f);
	auto len = strlen(log);
	if (log[len - 1] != '\n')
		fputs("\n", lf->f);
	fflush(lf->f);
	return rc;
}

static void file_target_dtor(struct log_file *lf) {
	fclose(lf->f);
}

static struct di_object *
file_target(struct di_log *l, const char *filename, bool overwrite) {
	FILE *f = fopen(filename, overwrite ? "w" : "a");
	if (!f)
		return di_new_error("Can't open %s for writing", filename);

	int fd = fileno(f);
	if (fd < 0) {
		fclose(f);
		return di_new_error("Can't get the file descriptor");
	}
	int ret = fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
	if (ret < 0) {
		fclose(f);
		return di_new_error("Can't set cloexec");
	}
	auto lf = di_new_object_with_type(struct log_file);
	lf->f = f;
	lf->dtor = (void *)file_target_dtor;
	di_method(lf, "write", file_target_write, char *);
	return (void *)lf;
}

static struct di_object *
stderr_target(struct di_log *l) {
	auto ls = di_new_object_with_type(struct log_file);
	ls->f = stderr;
	ls->dtor = NULL;
	di_method(ls, "write", file_target_write, char *);
	return (void *)ls;
}

// Public API to be used by C plugins
__attribute__((format(printf, 3, 4))) PUBLIC int
di_log_va(struct di_object *o, int log_level, const char *fmt, ...) {
	struct di_log *l = (void *)o;
	if (log_level > l->log_level)
		return 0;
	va_list ap;
	va_start(ap, fmt);
	int ret = vfprintf(stderr, fmt, ap);
	va_end(ap);

	return ret;
}

static char *get_log_level(struct di_log *l) {
	return strdup(level_tostring(l->log_level));
}

static int set_log_level(struct di_log *l, char *ll) {
	int nll = level_lookup(ll);
	if (nll <= DI_LOG_DEBUG) {
		l->log_level = nll;
		return 0;
	}
	return -1;
}

PUBLIC int di_set_log_level(struct di_object *o, int log_level) {
	if (log_level > DI_LOG_DEBUG)
		return -1;
	struct di_log *l = (void *)o;
	l->log_level = log_level;
	return 0;
}

void di_init_log(struct deai *di) {
	auto lm = di_new_module_with_size(di, sizeof(struct di_log));
	if (!lm)
		return;

	struct di_log *l = (void *)lm;
	l->log_level = DI_LOG_ERROR;

	auto dtgt = di_new_object_with_type(struct di_object);
	di_method(dtgt, "write", _stderr_write, char *);

	di_add_member_move((struct di_object *)l, "log_target",
	                   (di_type_t[]){DI_TYPE_OBJECT}, &dtgt);
	((struct di_object_internal *)l)->call = di_log;
	di_method(l, "file_target", file_target, char *, bool);
	di_method(l, "stderr_target", stderr_target);
	di_getter_setter(l, log_level, get_log_level, set_log_level);
	di_register_module(di, "log", &lm);
}
