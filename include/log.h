#pragma once
#include <object.h>

enum di_log_level {
	DI_LOG_ERROR,
	DI_LOG_WARN,
	DI_LOG_INFO,
	DI_LOG_DEBUG,
};

int di_log_va(struct di_object *o, int log_level, const char *fmt, ...);

#define di_get_log(v) object_cleanup struct di_object *log = (void *)di_find_module((v), "log")
