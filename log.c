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
static int di_log(struct di_object *o, char *log_level, const char *str) {
	struct di_log *l = (void *)o;
	if (level_lookup(log_level) > l->log_level)
		return 0;
	if (!str)
		str = "(nil)";
	return fputs(str, stderr);
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
	di_method(l, "log", di_log, char *, char *);
	di_register_module(di, "log", (void *)l);
	di_unref_object((void *)l);
}
