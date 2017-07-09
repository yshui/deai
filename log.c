/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <stdarg.h>
#include <stdio.h>

#include <deai/builtin/log.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include "di_internal.h"
#include "log.h"
#include "utils.h"

struct di_log {
	struct di_module;

	struct di_object *log_target;
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

// Function exposed via di_object to be used by any plugins
static int di_log(struct di_object *o, di_type_t *rt, void **ret, int nargs,
                  const di_type_t *atypes, const void *const *args) {
	if (nargs != 2)
		return -EINVAL;
	if (atypes[0] != DI_TYPE_STRING || atypes[1] != DI_TYPE_STRING)
		return -EINVAL;

	char *log_level = *(char **)args[0];
	char *str = *(char **)args[1];
	if (!str)
		str = "(nil)";
	*rt = DI_TYPE_NINT;
	int *res = *ret = malloc(di_sizeof_type(DI_TYPE_NINT));

	struct di_log *l = (void *)o;
	if (level_lookup(log_level) > l->log_level)
		return 0;
	if (!l->log_target)
		*res = fputs(str, stderr);
	else {
		int wrc = 0;
		int rc = di_callr(l->log_target, "write", wrc, str);
		if (rc != 0)
			*res = rc;
		else
			*res = wrc;
	}
	return 0;
}

struct log_file {
	struct di_object;
	FILE *f;
};

static int file_target_write(struct log_file *lf, char *log) {
	auto rc = fputs(log, lf->f);
	auto len = strlen(log);
	if (log[len-1] != '\n')
		fputs("\n", lf->f);
	return rc;
}

static void file_target_dtor(struct log_file *lf) {
	fclose(lf->f);
}

static struct di_object *file_target(struct di_log *l, const char *filename) {
	FILE *f = fopen(filename, "w");
	if (!f)
		return di_new_error("Can't open %s for writing", filename);

	auto lf = di_new_object_with_type(struct log_file);
	lf->f = f;
	lf->dtor = (void *)file_target_dtor;
	di_method(lf, "write", file_target_write, char *);
	return (void *)lf;
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

void di_init_log(struct deai *di) {
	auto l = di_new_module_with_type(struct di_log);
	if (!l)
		return;
	l->log_level = DI_LOG_ERROR;

	di_add_address_member((void *)l, "log_level", true, DI_TYPE_NINT,
	                      &l->log_level);
	di_add_address_member((void *)l, "log_target", true, DI_TYPE_OBJECT,
	                      &l->log_target);
	l->call = di_log;
	di_method(l, "file_target", file_target, char *);
	di_register_module(di, "log", (void *)l);
	di_unref_object((void *)l);
}
