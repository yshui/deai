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
#include "di_internal.h"

struct stack_annotate_context {
	Dwfl *dwfl;
	char *debuginfo_path;
	Dwfl_Callbacks callbacks;
};

struct stack_annotate_context *stack_trace_annotate_prepare() {
	auto ctx = tmalloc(struct stack_annotate_context, 1);

	ctx->callbacks = (Dwfl_Callbacks){
	    .find_elf = dwfl_linux_proc_find_elf,
	    .find_debuginfo = dwfl_standard_find_debuginfo,
	    .debuginfo_path = &ctx->debuginfo_path,
	};

	auto dwfl = dwfl_begin(&ctx->callbacks);
	if (dwfl == NULL || dwfl_linux_proc_report(dwfl, getpid()) != 0 ||
	    dwfl_report_end(dwfl, NULL, NULL) != 0) {
		// Give up
		if (dwfl) {
			dwfl_end(dwfl);
		}
		free(ctx);
		return NULL;
	}
	ctx->dwfl = dwfl;
	return ctx;
}

di_string stack_trace_annotate(struct stack_annotate_context *ctx, uint64_t ip) {
	// `ip` will normally be after the "call" instruction, so we move
	// it back to get the line number of the call.
	ip -= 1;
	di_string buf = DI_STRING_INIT;

	Dwarf_Addr addr = (uintptr_t)ip;
	Dwfl_Module *module = dwfl_addrmodule(ctx->dwfl, addr);
	const char *function_name = dwfl_module_addrname(module, addr);

	Dwfl_Line *line = dwfl_module_getsrc(module, addr);
	if (line != NULL) {
		int nline;
		Dwarf_Addr addr;
		const char *filename = dwfl_lineinfo(line, &addr, &nline, NULL, NULL, NULL);
		buf = di_string_printf("%s (%s:%d)", function_name, filename, nline);
	} else {
		buf = di_string_printf("%s (%#" PRIx64 ")", function_name, ip);
	}
	return buf;
}

void stack_trace_annotate_end(struct stack_annotate_context *ctx) {
	dwfl_end(ctx->dwfl);
	free(ctx);
}

unsigned int __attribute__((noinline))
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters
stack_trace_get(int skip, unsigned int limit, uint64_t *ips, uint64_t *procs,
                di_string *proc_names) {
	unw_context_t uc;
	unw_getcontext(&uc);

	unw_cursor_t cursor;
	unw_init_local(&cursor, &uc);

	unsigned int i = 0;
	while (unw_step(&cursor) > 0) {
		unw_word_t ip;
		unw_get_reg(&cursor, UNW_REG_IP, &ip);

		unw_word_t offset;
		unw_proc_info_t proc_info = {0};
		char name[128] = {0};
		// libunwind sometimes returns error despite actually have filled
		// the name in... so we ignore the return value.
		unw_get_proc_name(&cursor, name, sizeof(name), &offset);
		unw_get_proc_info(&cursor, &proc_info);

		if (skip <= 0) {
			ips[i] = ip;
			procs[i] = proc_info.start_ip;
			if (name[0]) {
				proc_names[i] = di_string_dup(name);
			} else {
				proc_names[i] = DI_STRING_INIT;
			}
			i += 1;
			if (i >= limit) {
				break;
			}
		} else {
			skip -= 1;
		}

		if (strcmp(name, "main") == 0) {
			break;
		}
	}
	return i;
}

unsigned int __attribute__((noinline)) stack_trace_frame_count(void) {
	unw_context_t uc;
	unw_getcontext(&uc);

	unw_cursor_t cursor;
	unw_init_local(&cursor, &uc);

	unsigned int i = 0;
	while (unw_step(&cursor) > 0) {
		i += 1;
	}
	return i;
}

void __attribute__((noinline))
// NOINLINENEXTLINE(bugprone-easily-swappable-parameters)
print_stack_trace(int skip, int limit) {
	uint64_t ips[limit];
	uint64_t procs[limit];
	di_string names[limit];
	unsigned int n = stack_trace_get(skip + 1, limit, ips, procs, names);
	auto dwfl = stack_trace_annotate_prepare();
	for (unsigned int i = 0; i < n; i++) {
		if (dwfl != NULL) {
			scoped_di_string buf = stack_trace_annotate(dwfl, ips[i]);
			di_log_va(log_module, DI_LOG_DEBUG, "  %.*s", (int)buf.length, buf.data);
		} else if (names[i].length > 0) {
			di_log_va(log_module, DI_LOG_DEBUG, "  %#16" PRIx64 " (%.*s)", ips[i],
			          (int)names[i].length, names[i].data);
			di_free_string(names[i]);
		} else {
			di_log_va(log_module, DI_LOG_DEBUG, "  %#16" PRIx64 " (%s)", ips[i], "??");
		}
	}
	if (dwfl != NULL) {
		stack_trace_annotate_end(dwfl);
	}
}
