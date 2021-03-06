/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>

#include <deai/builtins/log.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include "di_internal.h"
#include "log.h"
#include "utils.h"

struct di_log {
	struct di_module;

	int log_level;
};

static int level_lookup(struct di_string l) {
	if (strncasecmp(l.data, "error", l.length) == 0) {
		return DI_LOG_ERROR;
	}
	if (strncasecmp(l.data, "warn", l.length) == 0) {
		return DI_LOG_WARN;
	}
	if (strncasecmp(l.data, "info", l.length) == 0) {
		return DI_LOG_INFO;
	}
	if (strncasecmp(l.data, "debug", l.length) == 0) {
		return DI_LOG_DEBUG;
	}

	return DI_LOG_DEBUG + 1;
}

static const char *level_tostring(int log_level) {
	switch (log_level) {
	case DI_LOG_DEBUG:
		return "debug";
	case DI_LOG_INFO:
		return "info";
	case DI_LOG_WARN:
		return "warn";
	case DI_LOG_ERROR:
		return "error";
	default:
		return NULL;
	}
}

// Function exposed via di_object to be used by any plugins
static int di_log(struct di_object *o, di_type_t *rt, union di_value *ret, struct di_tuple t) {
	if (t.length != 3) {
		return -EINVAL;
	}
	if ((t.elements[1].type != DI_TYPE_STRING && t.elements[1].type != DI_TYPE_STRING_LITERAL) ||
	    (t.elements[2].type != DI_TYPE_STRING && t.elements[2].type != DI_TYPE_STRING_LITERAL)) {
		return -EINVAL;
	}

	struct di_string log_level, str;
	if (t.elements[1].type == DI_TYPE_STRING) {
		log_level = t.elements[1].value->string;
	} else {
		log_level = di_string_borrow(t.elements[1].value->string_literal);
	}
	if (t.elements[2].type == DI_TYPE_STRING) {
		str = t.elements[2].value->string;
	} else {
		str = di_string_borrow(t.elements[2].value->string_literal);
	}
	*rt = DI_TYPE_NINT;

	struct di_log *l = (void *)o;
	if (level_lookup(log_level) > l->log_level) {
		return 0;
	}

	with_object_cleanup(di_object) ltgt = NULL;
	if (di_get(l, "log_target", ltgt) != 0) {
		ret->nint = 0;
		return 0;
	}

	int wrc = 0;
	int rc = di_callr(ltgt, "write", wrc, str);
	if (rc != 0) {
		ret->nint = rc;
	} else {
		ret->nint = wrc;
	}
	return 0;
}

struct log_file {
	struct di_object_internal;
	FILE *f;
};

static int file_target_write(struct log_file *lf, struct di_string log) {
	auto rc = fwrite(log.data, 1, log.length, lf->f);
	if (log.data[log.length - 1] != '\n') {
		fputs("\n", lf->f);
	}
	fflush(lf->f);
	return rc;
}

static void file_target_dtor(struct log_file *lf) {
	fclose(lf->f);
}

static struct di_object *file_target(struct di_log *l, struct di_string filename, bool overwrite) {
	char filename_str[PATH_MAX];
	if (!di_string_to_chars(filename, filename_str, sizeof(filename_str))) {
		return di_new_error("Filename too long for file target");
	}

	FILE *f = fopen(filename_str, overwrite ? "w" : "a");
	if (!f) {
		return di_new_error("Can't open %s for writing", filename);
	}

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
	di_set_type((struct di_object *)lf, "deai.builtin.log:FileTarget");
	lf->f = f;
	lf->dtor = (void *)file_target_dtor;
	di_method(lf, "write", file_target_write, struct di_string);
	return (void *)lf;
}

static struct di_object *stderr_target(struct di_log *unused l) {
	auto ls = di_new_object_with_type(struct log_file);
	di_set_type((struct di_object *)ls, "deai.builtin.log:StderrTarget");
	ls->f = stderr;
	ls->dtor = NULL;
	di_method(ls, "write", file_target_write, struct di_string);
	return (void *)ls;
}

// Public API to be used by C plugins
int di_log_va(struct di_object *o, int log_level, const char *fmt, ...) {
	struct di_log *l = (void *)o;
	if (log_level > l->log_level) {
		return 0;
	}
	va_list ap;
	va_start(ap, fmt);
	int ret = vfprintf(stderr, fmt, ap);
	va_end(ap);

	return ret;
}

static const char *get_log_level(struct di_log *l) {
	return strdup(level_tostring(l->log_level));
}

static int set_log_level(struct di_log *l, struct di_string ll) {
	int nll = level_lookup(ll);
	if (nll <= DI_LOG_DEBUG) {
		l->log_level = nll;
		return 0;
	}
	return -1;
}

int di_set_log_level(struct di_object *o, int log_level) {
	if (log_level > DI_LOG_DEBUG) {
		return -1;
	}
	struct di_log *l = (void *)o;
	l->log_level = log_level;
	return 0;
}

struct di_object *log_module;
void di_init_log(struct deai *di) {
	auto lm = di_new_module_with_size(di, sizeof(struct di_log));
	if (!lm) {
		return;
	}
	di_set_type((struct di_object *)lm, "deai.builtin:LogModule");

	struct di_log *l = (void *)lm;
	l->log_level = DI_LOG_WARN;

	auto dtgt = stderr_target(l);

	di_add_member_move((struct di_object *)l, di_string_borrow("log_target"),
	                   (di_type_t[]){DI_TYPE_OBJECT}, &dtgt);
	((struct di_object_internal *)l)->call = di_log;
	di_method(l, "file_target", file_target, struct di_string, bool);
	di_method(l, "stderr_target", stderr_target);
	di_getter(l, log_level, get_log_level);
	di_setter(l, log_level, set_log_level, struct di_string);

	log_module = (struct di_object *)lm;
	di_register_module(di, di_string_borrow("log"), &lm);
}
