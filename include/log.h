#pragma once

enum di_log_level {
	DI_LOG_ERROR,
	DI_LOG_WARN,
	DI_LOG_INFO,
	DI_LOG_DEBUG,
};

struct di_object;

int di_log_va(struct di_object *o, int log_level, const char *fmt, ...);
