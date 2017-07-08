/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <ev.h>
#include <fcntl.h>
#include <unistd.h>

#include <deai/builtin/spawn.h>
#include <deai/helper.h>

#include "di_internal.h"
#include "spawn.h"
#include "string_buf.h"
#include "uthash.h"
#include "utils.h"

struct child {
	struct di_object;
	pid_t pid;

	ev_child w;
	ev_io outw, errw;

	struct string_buf *out, *err;
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
	if (!string_buf_is_empty(c->out)) {
		char *o = string_buf_dump(c->out);
		di_emit_from_object((void *)c, "stdout_line", o);
		free(o);
	}
	if (!string_buf_is_empty(c->err)) {
		char *o = string_buf_dump(c->err);
		di_emit_from_object((void *)c, "stderr_line", o);
		free(o);
	}
	di_emit_from_object((void *)c, "exit", ec, sig);

	ev_child_stop(EV_A_ & c->w);
	ev_io_stop(EV_A_ & c->outw);
	ev_io_stop(EV_A_ & c->errw);
	close(c->outw.fd);
	close(c->errw.fd);
	free(c->out);
	free(c->err);
	di_unref_object((void *)c);
}

static void
output_handler(struct child *c, int fd, struct string_buf *b, const char *ev) {
	static char buf[4096];
	int ret;
	while ((ret = read(fd, buf, sizeof(buf))) > 0) {
		char *pos = buf;
		while (1) {
			size_t len = buf + ret - pos;
			char *eol = memchr(pos, '\n', len), *out;
			if (eol) {
				*eol = '\0';
				if (!string_buf_is_empty(b)) {
					string_buf_push(b, pos);
					out = string_buf_dump(b);
					di_emit_from_object((void *)c, ev, out);
					free(out);
				} else
					di_emit_from_object((void *)c, ev, pos);
				pos = eol + 1;
			} else {
				string_buf_lpush(b, pos, len);
				break;
			}
		}
	}
}

static void stdout_cb(EV_P_ ev_io *w, int revents) {
	struct child *c = container_of(w, struct child, outw);
	output_handler(c, w->fd, c->out, "stdout_line");
}

static void stderr_cb(EV_P_ ev_io *w, int revents) {
	struct child *c = container_of(w, struct child, errw);
	output_handler(c, w->fd, c->err, "stderr_line");
}

define_trivial_cleanup(char *, free_charpp);

struct di_object *di_spawn_run(struct di_spawn *p, struct di_array argv) {
	if (argv.elem_type != DI_TYPE_STRING)
		return di_new_error("Invalid argv type");

	with_cleanup(free_charpp) char **nargv = tmalloc(char *, argv.length + 1);
	memcpy(nargv, argv.arr, sizeof(void *) * argv.length);

	int opfds[2], epfds[2];
	int ipfds[2];
	if (pipe(opfds) < 0 || pipe(epfds) < 0 || pipe(ipfds) < 0)
		return di_new_error("Failed to open pipe");

	close(ipfds[1]);

	if (fcntl(opfds[0], F_SETFL, O_NONBLOCK) < 0 ||
	    fcntl(epfds[0], F_SETFL, O_NONBLOCK) < 0)
		return di_new_error("Failed to fcntl");

	auto pid = fork();
	if (pid == 0) {
		if (dup2(ipfds[0], STDIN_FILENO) < 0 ||
		    dup2(opfds[1], STDOUT_FILENO) < 0 ||
		    dup2(epfds[1], STDERR_FILENO) < 0)
			_exit(1);
		execvp(nargv[0], nargv);
		_exit(1);
	}

	close(ipfds[0]);
	close(opfds[1]);
	close(epfds[1]);

	if (pid < 0)
		return di_new_error("Failed to fork");

	auto cp = di_new_object_with_type(struct child);
	cp->pid = pid;
	cp->out = string_buf_new();
	cp->err = string_buf_new();

	di_register_signal((void *)cp, "exit", 2,
	                   (di_type_t[]){DI_TYPE_NINT, DI_TYPE_NINT});
	di_register_signal((void *)cp, "stdout_line", 1,
	                   (di_type_t[]){DI_TYPE_STRING});
	di_register_signal((void *)cp, "stderr_line", 1,
	                   (di_type_t[]){DI_TYPE_STRING});

	ev_child_init(&cp->w, chld_handler, pid, 0);
	ev_child_start(p->di->loop, &cp->w);

	ev_io_init(&cp->outw, stdout_cb, opfds[0], EV_READ);
	ev_io_start(p->di->loop, &cp->outw);

	ev_io_init(&cp->errw, stderr_cb, epfds[0], EV_READ);
	ev_io_start(p->di->loop, &cp->errw);

	// child object can't die before the child process
	di_ref_object((void *)cp);

	return (void *)cp;
}

void di_init_spawn(struct deai *di) {
	auto m = di_new_module_with_type(struct di_spawn);
	m->di = di;

	// di_register_typed_method((void *)m, (di_fn_t)di_spawn_run, "run",
	//                         DI_TYPE_OBJECT, 1, DI_TYPE_ARRAY);
	di_method(m, "run", di_spawn_run, struct di_array);

	di_register_module(di, "spawn", (void *)m);
	di_unref_object((void *)m);
}
