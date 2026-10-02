/// Integration with LLVM/Clang's Coverage sanitizer, which inserts a callback on every
/// control flow edge.

// NOLINTBEGIN(bugprone-reserved-identifier)

#include <sanitizer/coverage_interface.h>
#include <stdint.h>
#include <stdio.h>
#include <threads.h>

#ifdef __clang__
#define __no_sanitize_coverage __attribute__((no_sanitize("coverage")))
#else
#define __no_sanitize_coverage __attribute__((no_sanitize_coverage))
#endif

/// Ring buffer keeping track of latest branch PCs
static thread_local uintptr_t pc_buf[1024];
static thread_local size_t tail = 0;
__no_sanitize_coverage void __sanitizer_cov_trace_pc(void) {
	uintptr_t pc = (uintptr_t)__builtin_return_address(0);
	pc_buf[tail] = pc;
	tail = (tail+1)%1024;
}

__no_sanitize_coverage void dump_trace(void) {
	char desc[1024];
	for (size_t head = (tail + 1) % 1024; head != tail; head = (head + 1) % 1024) {
		if (pc_buf[head] != 0) {
			__sanitizer_symbolize_pc((void *)pc_buf[head], "%p %F %L", desc, sizeof(desc));
			fprintf(stderr, "PC: %s\n", desc);
		} else {
			fprintf(stderr, "PC: NULL\n");
		}
	}
}

// NOLINTEND(bugprone-reserved-identifier)
