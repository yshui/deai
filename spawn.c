/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <ev.h>
#include <unistd.h>

#include <builtin/spawn.h>

#include "di_internal.h"
#include "spawn.h"
#include "uthash.h"
#include "utils.h"

struct child {
	struct di_object;
	pid_t pid;

	ev_child w;
};

struct di_spawn {
	struct di_module;

	struct child *children;
};

static void chld_handler(EV_P_ ev_child *w, int revents) {
	struct child *c = container_of(w, struct child, w);

	int sig = 0;
	if (WIFSIGNALED(w->rstatus))
		sig = WTERMSIG(w->rstatus);

	int ec = WEXITSTATUS(w->rstatus);
	di_emit_signal_v((void *)c, "exit", ec, sig);
}

struct di_object *di_spawn_run(struct di_spawn *p, struct di_array argv) {
	if (argv.elem_type != DI_TYPE_STRING)
		return di_new_error("Invalid argv type");

	char **nargv = tmalloc(char *, argv.length + 1);
	memcpy(nargv, argv.arr, sizeof(void *) * argv.length);

	auto pid = vfork();
	if (pid == 0) {
		execvp(nargv[0], nargv);
		_exit(1);
	}
	free(nargv);

	if (pid < 0)
		return di_new_error("Failed to fork");

	auto cp = di_new_object_with_type(struct child);
	cp->pid = pid;

	di_register_signal((void *)cp, "exit", 2, DI_TYPE_NINT, DI_TYPE_NINT);

	ev_child_init(&cp->w, chld_handler, pid, 0);
	ev_child_start(p->di->loop, &cp->w);

	return (void *)cp;
}

void di_init_spawn(struct deai *di) {
	auto m = di_new_module_with_type("spawn", struct di_spawn);
	m->di = di;

	di_register_typed_method(
	    (void *)m, di_create_typed_method((di_fn_t)di_spawn_run, "run",
	                                      DI_TYPE_OBJECT, 1, DI_TYPE_ARRAY));

	di_register_module(di, (void *)m);
}
