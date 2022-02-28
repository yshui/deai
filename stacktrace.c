#include <elfutils/libdwfl.h>
#include <libunwind.h>

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <deai/builtins/log.h>
#include <deai/deai.h>
#include "common.h"

static char *get_debug_info(const char *func, const void *ip) {
	char *debuginfo_path = NULL;

	Dwfl_Callbacks callbacks = {
	    .find_elf = dwfl_linux_proc_find_elf,
	    .find_debuginfo = dwfl_standard_find_debuginfo,
	    .debuginfo_path = &debuginfo_path,
	};

	char *buf = NULL;
	Dwfl *dwfl = dwfl_begin(&callbacks);
	if (dwfl == NULL || dwfl_linux_proc_report(dwfl, getpid()) != 0 ||
	    dwfl_report_end(dwfl, NULL, NULL) != 0) {
		// Give up
		asprintf(&buf, "%s (%p)", func, ip);
		goto out;
	}

	Dwarf_Addr addr = (uintptr_t)ip;
	Dwfl_Module *module = dwfl_addrmodule(dwfl, addr);
	const char *function_name = dwfl_module_addrname(module, addr);

	Dwfl_Line *line = dwfl_getsrc(dwfl, addr);
	if (line != NULL) {
		int nline;
		Dwarf_Addr addr;
		const char *filename = dwfl_lineinfo(line, &addr, &nline, NULL, NULL, NULL);
		if (strcmp(filename, "../object.c") == 0) {
			goto out;
		}
		asprintf(&buf, "%s (%s:%d)", function_name, filename, nline);
	} else {
		asprintf(&buf, "%s (%p)", function_name, ip);
	}
out:
	if (dwfl != NULL) {
		dwfl_end(dwfl);
	}
	return buf;
}

void __attribute__((noinline)) print_stack_trace(int skip, int limit) {
	unw_context_t uc;
	unw_getcontext(&uc);

	unw_cursor_t cursor;
	unw_init_local(&cursor, &uc);

	int skipped = 0;
	while (unw_step(&cursor) > 0) {
		unw_word_t ip;
		unw_get_reg(&cursor, UNW_REG_IP, &ip);

		unw_word_t offset;
		char name[64] = "??";
		unw_get_proc_name(&cursor, name, sizeof(name), &offset);

		if (skip <= 0) {
			// `ip` will normally be after the "call" instruction, so we move
			// it back to get the line number of the call.
			with_cleanup_t(char) detail = get_debug_info(name, (void *)(ip - 1));
			if (detail != NULL) {
				if (skipped) {
					di_log_va(log_module, DI_LOG_DEBUG, "\t(skipped %d)\n", skipped);
				}
				skipped = 0;
				di_log_va(log_module, DI_LOG_DEBUG, "\tat %s\n", detail);
				limit -= 1;
				if (limit <= 0) {
					break;
				}
			} else {
				skipped += 1;
			}
		}

		if (strcmp(name, "main") == 0) {
			break;
		}
		skip -= 1;
	}
}
