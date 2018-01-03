/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <ev.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/prctl.h>

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
	struct di_listener *d;
	struct deai *di;
};

struct di_spawn {
	struct di_module;

	struct child *children;
};

static inline void child_cleanup(struct child *c) {
	EV_P = c->di->loop;
	ev_child_stop(EV_A_ & c->w);
	if (c->out) {
		ev_io_stop(EV_A_ & c->outw);
		close(c->outw.fd);
		free(c->out);
	}
	if (c->err) {
		ev_io_stop(EV_A_ & c->errw);
		close(c->errw.fd);
		free(c->err);
	}
	di_clear_listeners((void *)c);

	struct deai *di = c->di;
	di_unref_object((void *)c);

	// It's fine if di's dtor is invoked on this stack frame
	di_unref_object((void *)di);
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
					di_emit(c, ev, out);
					free(out);
				} else
					di_emit(c, ev, pos);
				pos = eol + 1;
			} else {
				string_buf_lpush(b, pos, len);
				break;
			}
		}
	}
}

static void sigchld_handler(EV_P_ ev_child *w, int revents) {
	struct child *c = container_of(w, struct child, w);
	di_stop_listener(c->d);

	int sig = 0;
	if (WIFSIGNALED(w->rstatus))
		sig = WTERMSIG(w->rstatus);

	int ec = WEXITSTATUS(w->rstatus);
	if (c->out) {
		output_handler(c, c->outw.fd, c->out, "stdout_line");
		if (!string_buf_is_empty(c->out)) {
			char *o = string_buf_dump(c->out);
			di_emit(c, "stdout_line", o);
			free(o);
		}
	}
	if (c->err) {
		output_handler(c, c->errw.fd, c->err, "stderr_line");
		if (!string_buf_is_empty(c->err)) {
			char *o = string_buf_dump(c->err);
			di_emit(c, "stderr_line", o);
			free(o);
		}
	}
	di_emit(c, "exit", ec, sig);

	child_cleanup(c);
}

static void child_destroy(struct child *c) {
	kill(c->pid, SIGTERM);
	if (c->err)
		string_buf_clear(c->err);
	if (c->out)
		string_buf_clear(c->out);
	child_cleanup(c);
}

static void stdout_cb(EV_P_ ev_io *w, int revents) {
	struct child *c = container_of(w, struct child, outw);
	assert(c->out);
	output_handler(c, w->fd, c->out, "stdout_line");
}

static void stderr_cb(EV_P_ ev_io *w, int revents) {
	struct child *c = container_of(w, struct child, errw);
	assert(c->err);
	output_handler(c, w->fd, c->err, "stderr_line");
}

static uint64_t get_child_pid(struct child *c) {
	return (uint64_t)c->pid;
}

static void kill_child(struct child *c, int sig) {
	kill(c->pid, sig);
}

define_trivial_cleanup(char *, free_charpp);

struct di_object *
di_spawn_run(struct di_spawn *p, struct di_array argv, bool ignore_output) {
	if (argv.elem_type != DI_TYPE_STRING)
		return di_new_error("Invalid argv type");

	with_cleanup(free_charpp) char **nargv = tmalloc(char *, argv.length + 1);
	memcpy(nargv, argv.arr, sizeof(void *) * argv.length);

	int opfds[2], epfds[2], ifd;
	if (!ignore_output) {
		if (pipe(opfds) < 0 || pipe(epfds) < 0)
			return di_new_error("Failed to open pipe");

		if (fcntl(opfds[0], F_SETFD, FD_CLOEXEC) < 0 ||
		    fcntl(epfds[0], F_SETFD, FD_CLOEXEC) < 0)
			return di_new_error("Can't set cloexec");

		if (fcntl(opfds[0], F_SETFL, O_NONBLOCK) < 0 ||
		    fcntl(epfds[0], F_SETFL, O_NONBLOCK) < 0)
			return di_new_error("Can't set non block");
	} else {
		opfds[1] = open("/dev/null", O_WRONLY);
		epfds[1] = open("/dev/null", O_WRONLY);
		if (opfds[1] < 0 || epfds[1] < 0)
			return di_new_error("Can't open /dev/null");
	}
	ifd = open("/dev/null", O_RDONLY);
	if (ifd < 0)
		return di_new_error("Can't open /dev/null");

	auto pid = fork();
	if (pid == 0) {
		if (!ignore_output) {
			close(opfds[0]);
			close(epfds[0]);
		}
		if (dup2(ifd, STDIN_FILENO) < 0 || dup2(opfds[1], STDOUT_FILENO) < 0 ||
		    dup2(epfds[1], STDERR_FILENO) < 0)
			_exit(1);
		close(opfds[1]);
		close(epfds[1]);
		close(ifd);

		execvp(nargv[0], nargv);
		_exit(1);
	}

	close(ifd);
	close(opfds[1]);
	close(epfds[1]);

	if (pid < 0)
		return di_new_error("Failed to fork");

	auto cp = di_new_object_with_type(struct child);
	di_method(cp, "__get_pid", get_child_pid);
	di_method(cp, "kill", kill_child, int);
	cp->pid = pid;
	if (!ignore_output) {
		cp->out = string_buf_new();
		cp->err = string_buf_new();

		ev_io_init(&cp->outw, stdout_cb, opfds[0], EV_READ);
		ev_io_start(p->di->loop, &cp->outw);

		ev_io_init(&cp->errw, stderr_cb, epfds[0], EV_READ);
		ev_io_start(p->di->loop, &cp->errw);
	}
	cp->di = p->di;

	ev_child_init(&cp->w, sigchld_handler, pid, 0);
	ev_child_start(p->di->loop, &cp->w);

	cp->d =
	    di_listen_to_destroyed((void *)p->di, (void *)child_destroy, (void *)cp);

	// child object can't die before the child process
	di_ref_object((void *)cp);
	di_ref_object((void *)cp->di);

	return (void *)cp;
}

void di_init_spawn(struct deai *di) {
	// Become subreaper
	int ret = prctl(PR_SET_CHILD_SUBREAPER, 1);
	if (ret != 0)
		return;

	auto m = di_new_module_with_type(struct di_spawn);
	m->di = di;

	// di_register_typed_method((void *)m, (di_fn_t)di_spawn_run, "run",
	//                         DI_TYPE_OBJECT, 1, DI_TYPE_ARRAY);
	di_method(m, "run", di_spawn_run, struct di_array, bool);

	di_register_module(di, "spawn", (void *)m);
}
