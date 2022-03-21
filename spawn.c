/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <ev.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <sys/procctl.h>
#else
#include <sys/prctl.h>
#endif

#include <deai/builtins/spawn.h>
#include <deai/helper.h>

#include "di_internal.h"
#include "spawn.h"
#include "string_buf.h"
#include "uthash.h"
#include "utils.h"

/// Object type: ChildProcess
///
/// Represent a child process. When recycled, the child process will be left running. To
/// stop the process, you have to explicitly call `kill`
///
/// Signals:
/// * stderr_line(line: string) a line has been written to stderr by the child
/// * stdout_line(line: string) a line has been written to stdout by the child
/// * exit(exit_code, signal) the child process has exited
struct child {
	struct di_object;
	pid_t pid;

	ev_child w;
	int fds[2];

	struct string_buf *output_buf[2];
};

struct di_spawn {
	struct di_module;

	struct child *children;
};

static void output_handler(struct child *c, int fd, int id, const char *ev) {
	static char buf[4096];
	int ret;
	while ((ret = read(fd, buf, sizeof(buf))) > 0) {
		const char *pos = buf;
		while (1) {
			size_t len = buf + ret - pos;
			char *eol = memchr(pos, '\n', len);
			if (eol) {
				*eol = '\0';
				if (!string_buf_is_empty(c->output_buf[id])) {
					string_buf_push(c->output_buf[id], pos);

					const char *out = string_buf_dump(c->output_buf[id]);
					di_emit(c, ev, out);
					free((char *)out);
				} else {
					di_emit(c, ev, pos);
				}
				pos = eol + 1;
			} else {
				string_buf_lpush(c->output_buf[id], pos, len);
				break;
			}
			if (c->output_buf[id] == NULL) {
				// Signal listeners have stopped after we emitted the
				// previous signal, so we should stop as well
				return;
			}
		}
	}
}

static const char *const SIGNAL_NAME[] = {"stdout_line", "stderr_line"};
/// SIGNAL: deai.builtin.spawn:ChildProcess.stdout_line(line: :string) The child process
/// wrote one line to stdout.
///
/// Only generated if "ignore_output" wasn't set to true.
///
/// SIGNAL: deai.builtin.spawn:ChildProcess.stderr_line(line: :string) The child process
/// wrote one line to stderr.
///
/// Only generated if "ignore_output" wasn't set to true.
///
/// SIGNAL: deai.builtin.spawn:ChildProcess.exit(exit_code: :integer, signal: :integer)
/// The child process exited.
static void sigchld_handler(EV_P_ ev_child *w, int revents) {
	struct child *c = container_of(w, struct child, w);
	// Keep child process object alive when emitting
	di_object_with_cleanup unused obj = di_ref_object((struct di_object *)c);

	int sig = 0;
	if (WIFSIGNALED(w->rstatus)) {
		sig = WTERMSIG(w->rstatus);
	}

	int ec = WEXITSTATUS(w->rstatus);
	for (int i = 0; i < 2; i++) {
		if (c->output_buf[i]) {
			output_handler(c, c->fds[i], i, SIGNAL_NAME[i]);
			// output_handler might caused the output_buf to be freed.
			if (c->output_buf[i] && !string_buf_is_empty(c->output_buf[i])) {
				const char *o = string_buf_dump(c->output_buf[i]);
				di_emit(c, SIGNAL_NAME[i], o);
				free((char *)o);
			}
		}
	}
	di_emit(c, "exit", ec, sig);

	// Proactively stop all signal listeners.
	di_remove_member((void *)c, di_string_borrow("__signal_stdout_line"));
	di_remove_member((void *)c, di_string_borrow("__signal_stderr_line"));
	di_remove_member((void *)c, di_string_borrow("__signal_exit"));
}

static void child_destroy(struct di_object *obj) {
	auto c = (struct child *)obj;
	for (int i = 0; i < 2; i++) {
		if (c->output_buf[i]) {
			string_buf_clear(c->output_buf[i]);
			free(c->output_buf[i]);
		}
		close(c->fds[i]);
	}
}

static void output_cb(struct di_object *obj, int id) {
	auto c = (struct child *)obj;
	assert(c->output_buf[id]);
	output_handler(c, c->fds[id], id, SIGNAL_NAME[id]);
}

/// Pid of the child process
///
/// EXPORT: deai.builtin.spawn:ChildProcess.pid, :integer
static uint64_t get_child_pid(struct child *c) {
	return (uint64_t)c->pid;
}

/// Send signal to child process
///
/// EXPORT: deai.builtin.spawn:ChildProcess.kill(signal: :integer), :void
static void kill_child(struct child *c, int sig) {
	kill(c->pid, sig);
}

static struct di_object *di_setup_fds(bool ignore_output, int *opfds, int *epfds, int *ifd) {
	opfds[0] = opfds[1] = -1;
	epfds[0] = epfds[1] = -1;
	*ifd = -1;

	struct di_object *ret = NULL;
	do {
		if (!ignore_output) {
			if (pipe(opfds) < 0 || pipe(epfds) < 0) {
				ret = di_new_error("Failed to open pipe");
				break;
			}

			if (fcntl(opfds[0], F_SETFD, FD_CLOEXEC) < 0 ||
			    fcntl(epfds[0], F_SETFD, FD_CLOEXEC) < 0) {
				ret = di_new_error("Can't set cloexec");
				break;
			}

			if (fcntl(opfds[0], F_SETFL, O_NONBLOCK) < 0 ||
			    fcntl(epfds[0], F_SETFL, O_NONBLOCK) < 0) {
				ret = di_new_error("Can't set non block");
				break;
			}
		} else {
			opfds[1] = open("/dev/null", O_WRONLY);
			epfds[1] = open("/dev/null", O_WRONLY);
			if (opfds[1] < 0 || epfds[1] < 0) {
				ret = di_new_error("Can't open /dev/null");
				break;
			}
		}
		*ifd = open("/dev/null", O_RDONLY);
		if (*ifd < 0) {
			ret = di_new_error("Can't open /dev/null");
			break;
		}
	} while (0);

	if (ret != NULL) {
		close(opfds[0]);
		close(opfds[1]);
		close(epfds[0]);
		close(epfds[1]);
		close(*ifd);
	}
	return ret;
}

static void di_child_process_new_exit_signal(struct di_object *p, struct di_object *sig) {
	if (di_member_clone(p, "__signal_exit", sig) != 0) {
		return;
	}

	auto child = (struct child *)p;
	di_object_with_cleanup di_obj = di_object_get_deai_weak(p);
	if (di_obj == NULL) {
		return;
	}

	auto di = (struct deai *)di_obj;
	ev_child_init(&child->w, sigchld_handler, child->pid, 0);
	ev_child_start(di->loop, &child->w);

	// Add ourselves to root since we are now a fundamental event source.
	di_object_upgrade_deai(p);

	auto roots = di_get_roots();
	di_string_with_cleanup child_root_key =
	    di_string_printf("child_process_%d", child->pid);
	DI_CHECK_OK(di_call(roots, "add", child_root_key, p));
}

static void di_child_start_output_listener(struct di_object *p, int id) {
	di_object_with_cleanup di_obj = di_object_get_deai_weak(p);
	if (di_obj == NULL) {
		return;
	}

	auto c = (struct child *)p;

	di_object_with_cleanup event_module;
	DI_CHECK_OK(di_get(di_obj, "event", event_module));

	di_object_with_cleanup fdevent;
	DI_CHECK_OK(di_callr(event_module, "fdevent", fdevent, c->fds[id]));

	di_object_with_cleanup closure = (void *)di_closure(output_cb, (p, id));
	auto listen_handle = di_listen_to(fdevent, di_string_borrow("read"), closure);

	DI_CHECK_OK(di_call(listen_handle, "auto_stop", true));

	di_string_with_cleanup listen_handle_key =
	    di_string_printf("__listen_handle_for_output_%d", id);
	di_add_member_move(p, listen_handle_key, (di_type_t[]){DI_TYPE_OBJECT}, &listen_handle);
	c->output_buf[id] = string_buf_new();
}

static void di_child_process_new_stdout_signal(struct di_object *p, struct di_object *sig) {
	if (di_member_clone(p, "__signal_stdout_line", sig) != 0) {
		return;
	}
	di_child_start_output_listener(p, 0);
}

static void di_child_process_new_stderr_signal(struct di_object *p, struct di_object *sig) {
	if (di_member_clone(p, "__signal_stderr_line", sig) != 0) {
		return;
	}
	di_child_start_output_listener(p, 1);
}

static void di_child_process_delete_exit_signal(struct di_object *obj) {
	if (di_remove_member_raw(obj, di_string_borrow("__signal_exit")) != 0) {
		return;
	}
	auto c = (struct child *)obj;
	di_object_with_cleanup di_obj = di_object_get_deai_strong((struct di_object *)c);

	auto di = (struct deai *)di_obj;
	EV_P = di->loop;
	ev_child_stop(EV_A_ & c->w);

	// We as a fundamental event source has stopped, so remove roots and unref core.
	auto roots = di_get_roots();
	di_string_with_cleanup child_root_key = di_string_printf("child_process_%d", c->pid);
	DI_CHECK_OK(di_call(roots, "remove", child_root_key));

	di_object_downgrade_deai((void *)c);
}

static void di_child_process_stop_output_listener(struct di_object *obj, int id) {
	di_string_with_cleanup listen_handle_key =
	    di_string_printf("__listen_handle_for_output_%d", id);
	DI_CHECK_OK(di_remove_member_raw(obj, listen_handle_key));

	auto c = (struct child *)obj;
	string_buf_clear(c->output_buf[id]);
	free(c->output_buf[id]);
	c->output_buf[id] = NULL;
}

static void di_child_process_delete_stdout_signal(struct di_object *obj) {
	if (di_remove_member_raw(obj, di_string_borrow("__signal_stdout_line")) == 0) {
		di_child_process_stop_output_listener(obj, 0);
	}
}

static void di_child_process_delete_stderr_signal(struct di_object *obj) {
	if (di_remove_member_raw(obj, di_string_borrow("__signal_stderr_line")) == 0) {
		di_child_process_stop_output_listener(obj, 1);
	}
}

/// Start a child process
///
/// EXPORT: spawn.run(argv, ignore_output: :bool), deai.builtin.spawn:ChildProcess
///
/// Arguments:
///
/// - argv([:string]) arguments passed to command
/// - ignore_output if true, outputs of the child process will be redirected to
///                 :code:`/dev/null`. if this is false, you have to handle the signals to
///                 avoid the program's output from being blocked.
///
/// Returns an object representing the child process.
struct di_object *di_spawn_run(struct di_spawn *p, struct di_array argv, bool ignore_output) {
	if (argv.elem_type != DI_TYPE_STRING) {
		return di_new_error("Invalid argv type");
	}
	di_object_with_cleanup obj = di_module_get_deai((struct di_module *)p);
	if (obj == NULL) {
		return di_new_error("deai is shutting down...");
	}

	int opfds[2], epfds[2], ifd;
	auto ret = di_setup_fds(ignore_output, opfds, epfds, &ifd);
	if (ret != NULL) {
		return ret;
	}

	char **nargv = tmalloc(char *, argv.length + 1);
	struct di_string *strings = argv.arr;
	for (int i = 0; i < argv.length; i++) {
		nargv[i] = di_string_to_chars_alloc(strings[i]);
	}

	auto pid = fork();
	if (pid == 0) {
		if (!ignore_output) {
			close(opfds[0]);
			close(epfds[0]);
		}
		if (dup2(ifd, STDIN_FILENO) < 0 || dup2(opfds[1], STDOUT_FILENO) < 0 ||
		    dup2(epfds[1], STDERR_FILENO) < 0) {
			_exit(1);
		}
		close(opfds[1]);
		close(epfds[1]);
		close(ifd);

		execvp(nargv[0], nargv);
		_exit(1);
	}

	for (int i = 0; i < argv.length; i++) {
		free(nargv[i]);
	}
	free(nargv);

	close(ifd);
	close(opfds[1]);
	close(epfds[1]);

	if (pid < 0) {
		close(opfds[0]);
		close(epfds[0]);
		return di_new_error("Failed to fork");
	}

	auto cp = di_new_object_with_type(struct child);
	di_set_type((struct di_object *)cp, "deai.builtin.spawn:ChildProcess");
	di_set_object_dtor((struct di_object *)cp, child_destroy);
	di_method(cp, "__get_pid", get_child_pid);
	di_method(cp, "kill", kill_child, int);
	di_method(cp, "__set___signal_exit", di_child_process_new_exit_signal,
	          struct di_object *);
	di_method(cp, "__set___signal_stdout_line", di_child_process_new_stdout_signal,
	          struct di_object *);
	di_method(cp, "__set___signal_stderr_line", di_child_process_new_stderr_signal,
	          struct di_object *);
	di_method(cp, "__delete___signal_exit", di_child_process_delete_exit_signal);
	di_method(cp, "__delete___signal_stdout_line", di_child_process_delete_stdout_signal);
	di_method(cp, "__delete___signal_stderr_line", di_child_process_delete_stderr_signal);

	cp->pid = pid;
	cp->fds[0] = opfds[0];
	cp->fds[1] = epfds[0];

	// Keep a reference from the ChildProcess object to deai, to keep it alive
	auto weak_di = di_weakly_ref_object(obj);
	di_member(cp, DEAI_MEMBER_NAME_RAW, weak_di);
	return (void *)cp;
}

/// Spawn child processes
///
/// EXPORT: spawn, deai:module
void di_init_spawn(struct deai *di) {
	// Become subreaper
#ifdef __FreeBSD__
	int ret = procctl(P_PID, getpid(), PROC_REAP_ACQUIRE, NULL);
#else
	int ret = prctl(PR_SET_CHILD_SUBREAPER, 1);
#endif
	if (ret != 0) {
		return;
	}

	auto m = di_new_module_with_size(di, sizeof(struct di_spawn));
	di_method(m, "run", di_spawn_run, struct di_array, bool);

	di_register_module(di, di_string_borrow("spawn"), &m);
}
