/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtins/log.h>
#include <deai/callable.h>
#include <deai/error.h>
#include <deai/helper.h>
#include <deai/object.h>
#include <deai/type.h>
#include <assert.h>
#include <ev.h>
#include <inttypes.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/mman.h>
#include "di_internal.h"
#include "utils.h"

#define HANDLER_PREFIX "handler_object_"
struct di_signal {
	di_object;
	int nhandlers;
};

// This is essentially a fat weak reference, from object to its listeners.
struct di_listener {
	struct di_weak_object *listen_handle;
	struct list_head siblings;
};

static di_object_internal dead_weakly_referenced_object = {
    .ref_count = 0,
    .weak_ref_count = 1,        // Keep this object from being freed
};

struct di_weak_object *const dead_weak_ref =
    (struct di_weak_object *)&dead_weakly_referenced_object;

const void *null_ptr = NULL;
// clang-format off
static_assert(sizeof(di_object) == sizeof(di_object_internal),
              "di_object size mismatch");
static_assert(alignof(di_object) == alignof(di_object_internal),
              "di_object alignment mismatch");
// clang-format on

static const char error_type[] = "deai:Error";

di_string di_error_to_string(di_object *err) {
	di_string func = DI_STRING_INIT, file = DI_STRING_INIT;
	int line = -1;
	di_rawget_borrowed(err, "func", func);
	di_rawget_borrowed(err, "file", file);
	di_rawget_borrowed(err, "line", line);
	scoped_di_array parts_arr = {
	    .arr = tmalloc(di_string, 5),
	    .elem_type = DI_TYPE_STRING,
	};
	di_string *parts = parts_arr.arr;
	di_object *source = NULL;
	di_rawget_borrowed(err, "source", source);
	unsigned pos = 0;
	if (source != NULL) {
		parts[pos++] = di_error_to_string(source);
		parts[pos++] = di_string_dup(
		    "\nWhile handling the above error, the following error occurred:\n");
	}
	if (func.length || file.length || line > 0) {
		if (func.length == 0) {
			func = di_string_borrow_literal("<unknown>");
		}
		if (file.length == 0) {
			file = di_string_borrow_literal("??");
		}
		parts[pos++] = di_string_printf("error caught in \"%.*s\" (%.*s:%d): ", (int)func.length,
		                                func.data, (int)file.length, file.data, line);
	} else {
		parts[pos++] = di_string_dup("error caught: ");
	}

	if (di_rawgetxt(err, di_string_borrow_literal("error"), DI_TYPE_STRING,
	                (di_value *)&parts[pos]) != 0) {
		parts[pos] = di_string_dup("<unknown error>");
	}
	pos++;

	if (di_rawgetxt(err, di_string_borrow_literal("detail"), DI_TYPE_STRING,
	                (di_value *)&parts[pos]) != 0) {
		parts[pos] = di_string_dup("(no detail)");
	}
	pos++;

#ifdef ENABLE_STACK_TRACE
	di_array ips = {0}, procs = {0}, names = {0};
	if (di_rawget_borrowed(err, "stack_ips", ips) == 0 &&
	    di_rawget_borrowed(err, "stack_procs", procs) == 0 &&
	    di_rawget_borrowed(err, "stack_proc_names", names) == 0) {
		auto ctx = stack_trace_annotate_prepare();
		uint64_t *ips_arr = ips.arr, *procs_arr = procs.arr;
		di_string *names_arr = names.arr;
		parts = realloc(parts_arr.arr, sizeof(di_string) * (pos + ips.length + 1));
		if (!parts) {
			goto stack_trace_out;
		}
		parts_arr.arr = parts;
		parts[pos++] = di_string_dup("\nstack traceback:");
		for (size_t i = 0; i < ips.length; i++) {
			if (ctx) {
				scoped_di_string annotated = stack_trace_annotate(ctx, ips_arr[i]);
				parts[i + pos] = di_string_printf("%10zu: %#16" PRIx64 " %.*s", i, ips_arr[i],
				                                  (int)annotated.length, annotated.data);
			} else {
				auto offsets = procs_arr[i] != 0 ? ips_arr[i] - procs_arr[i] : 0;
				auto proc_name = names_arr[i];
				if (proc_name.length == 0) {
					proc_name = di_string_borrow_literal("<unknown>");
				}
				if (offsets) {
					parts[i + pos] =
					    di_string_printf("%10zu: %#16" PRIx64 "(%.*s+%#lx)", i, ips_arr[i],
					                     (int)proc_name.length, proc_name.data, offsets);
				} else {
					parts[i + pos] =
					    di_string_printf("%10zu%#16" PRIx64 "(%.*s)", i, ips_arr[i],
					                     (int)proc_name.length, proc_name.data);
				}
			}
		}
		pos += ips.length;
		if (ctx) {
			stack_trace_annotate_end(ctx);
		}
	}
stack_trace_out:
#endif
	parts_arr.length = pos;

	di_string ret = di_string_join(parts_arr, di_string_borrow_literal("\n"));
	return ret;
}

di_object *
di_new_error_from_string(const char *file, int line, const char *func, di_string message) {
	auto err = di_new_object_with_type(di_object);
	di_set_type(err, error_type);

#ifdef ENABLE_STACK_TRACE
	auto frame_count = stack_trace_frame_count();
	di_array ips = {
	    .arr = tmalloc(uint64_t, frame_count),
	    .elem_type = DI_TYPE_UINT,
	};
	di_array procs = {
	    .arr = tmalloc(uint64_t, frame_count),
	    .elem_type = DI_TYPE_UINT,
	};
	di_array names = {
	    .arr = tmalloc(di_string, frame_count),
	    .elem_type = DI_TYPE_STRING,
	};
	frame_count = stack_trace_get(0, frame_count, ips.arr, procs.arr, names.arr);
	uint64_t *procs_arr = procs.arr;
	for (unsigned int i = 0; i < frame_count; i++) {
		if (procs_arr[i] != (uintptr_t)di_new_error_from_string &&
		    procs_arr[i] != (uintptr_t)di_new_error2) {
			if (i > 0) {
				for (unsigned int j = 0; j < i; j++) {
					di_free_string(((di_string *)names.arr)[j]);
				}
				memmove(ips.arr, ips.arr + i * sizeof(uint64_t),
				        (frame_count - i) * sizeof(uint64_t));
				memmove(procs.arr, procs.arr + i * sizeof(uint64_t),
				        (frame_count - i) * sizeof(uint64_t));
				memmove(names.arr, names.arr + i * sizeof(di_string),
				        (frame_count - i) * sizeof(di_string));
			}
			ips.length = frame_count - i;
			procs.length = frame_count - i;
			names.length = frame_count - i;
			break;
		}
	}
	di_member(err, "stack_ips", ips);
	di_member(err, "stack_procs", procs);
	di_member(err, "stack_proc_names", names);
#endif

	di_method(err, "__to_string", di_error_to_string);
	di_member(err, "error", message);
	if (file) {
		di_member_clone(err, "file", di_string_borrow(file));
	}
	if (line >= 0) {
		di_member(err, "line", line);
	}
	if (func) {
		di_member_clone(err, "func", di_string_borrow(func));
	}
	return err;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
di_object *di_new_error2(const char *file, int line, const char *func, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	di_string msg;
	int ret = vasprintf((char **)&msg.data, fmt, ap);
	if (ret < 0) {
		msg = di_string_dup(fmt);
	} else {
		msg.length = strlen(msg.data);
	}
	return di_new_error_from_string(file, line, func, msg);
}

bool di_is_error(di_object *obj) {
	return di_check_type(obj, error_type);
}

static int di_call_internal(di_object *self, di_object *method_, di_type *rt,
                            di_value *ret, di_tuple args, bool *called) {
	auto method = (di_object_internal *)method_;
	*called = false;
	if (!method->call) {
		return -EINVAL;
	}

	di_tuple real_args;
	real_args.length = args.length + 1;
	assert(real_args.length <= MAX_NARGS);

	/* Push the object itself as the first argument */
	real_args.elements = alloca(sizeof(struct di_variant) * real_args.length);
	memcpy(&real_args.elements[1], args.elements, sizeof(struct di_variant) * args.length);
	real_args.elements[0].type = DI_TYPE_OBJECT;
	real_args.elements[0].value = (di_value *)&self;

	int rc = method->call(method_, rt, ret, real_args);
	*called = true;

	di_unref_object(method_);
	return rc;
}

int di_callx(di_object *self, di_string name, di_type *rt, di_value *ret, di_tuple args,
             bool *called) {
	di_object *val;
	*called = 0;
	int rc = di_getxt(self, name, DI_TYPE_OBJECT, (di_value *)&val, NULL);
	if (rc != 0) {
		return rc;
	}
	return di_call_internal(self, val, rt, ret, args, called);
};

/// Call "<prefix>_<name>" with "<prefix>" as fallback
///
/// @param[out] found whether a handler is found
static int call_handler_with_fallback(di_object *nonnull o, const char *nonnull prefix,
                                      di_string name, struct di_variant arg,
                                      di_type *nullable rtype, di_value *nullable ret,
                                      di_object *nullable *nullable error, bool *found) {
	*found = false;

	scoped_di_string buf = di_string_printf("%s_%.*s", prefix, (int)name.length, name.data);
	di_type rtype2 = DI_LAST_TYPE;
	di_value ret2;

	struct di_variant args[3] = {
	    {.type = DI_TYPE_OBJECT, .value = (di_value *)&o},
	    {.type = arg.type, .value = arg.value},
	};
	di_tuple tmp = {
	    // There is a trick here.
	    // DI_LAST_TYPE is used to signify that the argument "doesn't exist". Unlike
	    // DI_TYPE_NIL, which would mean there is one argument, whose type is nil.
	    // This is not a convention. Generally speaking, DI_LAST_TYPE should never
	    // appear in argument lists.
	    .length = arg.type != DI_LAST_TYPE ? 2 : 1,
	    .elements = args,
	};
	di_object *handler = NULL;
	int rc = -ENOENT;
	if (di_rawget_borrowed2(o, buf, handler) == 0) {
		*found = true;
		if (error != NULL) {
			rc = di_call_object_catch(handler, &rtype2, &ret2, tmp, error);
		} else {
			rc = di_call_object(handler, &rtype2, &ret2, tmp);
		}
	}

	if (rc != 0 && di_rawget_borrowed(o, prefix, handler) == 0) {
		*found = true;
		tmp.length++;
		if (tmp.length > 2) {
			args[2] = args[1];
		}
		args[1] = (struct di_variant){
		    .type = DI_TYPE_STRING,
		    .value = (di_value[]){{.string = name}},
		};
		if (error != NULL) {
			rc = di_call_object_catch(handler, &rtype2, &ret2, tmp, error);
		} else {
			rc = di_call_object(handler, &rtype2, &ret2, tmp);
		}
	}

	if (rc == 0) {
		if (ret && rtype) {
			*rtype = rtype2;
			*ret = ret2;
		} else {
			di_free_value(rtype2, &ret2);
		}
	}
	return rc;
}

int di_setx(di_object *o, di_string prop, di_type type, const void *val,
            di_object *nullable *nullable error) {
	// If a setter is present, we just call that and we are done.
	bool handler_found;
	int rc = call_handler_with_fallback(o, "__set", prop,
	                                    (struct di_variant){(di_value *)val, type}, NULL,
	                                    NULL, error, &handler_found);
	if (handler_found) {
		return rc;
	}

	// Call the deleter if present
	rc = call_handler_with_fallback(o, "__delete", prop, (struct di_variant){NULL, DI_LAST_TYPE},
	                                NULL, NULL, error, &handler_found);
	if (handler_found && rc != 0) {
		return rc;
	}

	// Finally, replace the value
	auto mem = di_lookup(o, prop);
	if (mem) {
		// the old member still exists, we need to drop the old value
		di_free_value(mem->type, mem->data);

		mem->data = realloc(mem->data, di_sizeof_type(type));
		mem->type = type;
		di_copy_value(mem->type, mem->data, val);
		return 0;
	}

	return di_add_member_clone(o, prop, type, val);
}

int di_refrawgetx(di_object *o, di_string prop, di_type *type, di_value **ret) {
	auto m = di_lookup(o, prop);

	// nil type is treated as non-existent
	if (!m) {
		return -ENOENT;
	}

	assert(di_sizeof_type(m->type) != 0);
	*type = m->type;
	*ret = m->data;
	return 0;
}

int di_rawgetx(di_object *o, di_string prop, di_type *type, di_value *ret) {
	di_value *tmp = NULL;
	int rc = di_refrawgetx(o, prop, type, &tmp);
	if (rc != 0) {
		return rc;
	}
	di_copy_value(*type, ret, tmp);
	return 0;
}

/// A di_call_fn that does nothing.
int di_noop(di_object *o, di_type *rt, di_value *r, di_tuple args) {
	return 0;
}

/// Decide if `val` contains DI_LAST_TYPE, and if so, free `val`.
/// Ordinary `di_free_value` cannot free value of DI_LAST_TYPE, this function should only
/// be used when calling getter functions.
static bool di_free_last_type(di_type type, di_value *val) {
	bool ret = false;
	if (type == DI_LAST_TYPE) {
		return true;
	}
	if (type == DI_TYPE_VARIANT) {
		// In case of a variant, we need to check the inner type.
		ret = di_free_last_type(val->variant.type, val->variant.value);
		if (ret) {
			free(val->variant.value);
		}
	}
	return ret;
}

int di_getx(di_object *o, di_string prop, di_type *type, di_value *ret,
            di_object *nullable *nullable error) {
	int rc = di_rawgetx(o, prop, type, ret);
	if (rc == 0) {
		return 0;
	}

	bool handler_found;
	rc = call_handler_with_fallback(o, "__get", prop, (struct di_variant){NULL, DI_LAST_TYPE},
	                                type, ret, error, &handler_found);
	if (rc != 0) {
		return rc;
	}

	if (error && *error) {
		return 0;
	}

	if (di_free_last_type(*type, ret)) {
		return -ENOENT;
	}
	return 0;
}

int di_getxt(di_object *o, di_string prop, di_type rtype, di_value *ret,
             di_object *nullable *nullable error) {
	di_value ret2;
	di_type rt;
	int rc = di_getx(o, prop, &rt, &ret2, error);
	if (rc != 0) {
		return rc;
	}
	rc = di_type_conversion(rt, &ret2, rtype, ret, 0);
	return rc;
};
int di_rawgetxt(di_object *o, di_string prop, di_type rtype, di_value *ret) {
	di_value ret2;
	di_type rt;
	int rc = di_rawgetx(o, prop, &rt, &ret2);
	if (rc != 0) {
		return rc;
	}
	rc = di_type_conversion(rt, &ret2, rtype, ret, 0);
	return rc;
};

int di_set_type(di_object *o, const char *type) {
	return di_rawsetx(o, di_string_borrow_literal("__type"), DI_TYPE_STRING_LITERAL, &type);
}

const char *di_get_type(di_object *o) {
	const char *ret;
	int rc = di_rawgetxt(o, di_string_borrow_literal("__type"), DI_TYPE_STRING_LITERAL,
	                     (di_value *)&ret);
	if (rc != 0) {
		if (rc == -ENOENT) {
			return "deai:object";
		}
		return ERR_PTR(rc);
	}

	return ret;
}

bool di_check_type(di_object *o, const char *tyname) {
	const char *ot = di_get_type(o);
	if (IS_ERR_OR_NULL(ot)) {
		return false;
	}

	return strcmp(ot, tyname) == 0;
}

#ifdef TRACK_OBJECTS
thread_local struct list_head all_objects;
struct di_ref_tracked_object *ref_tracked;
#endif

/// Objects that has been unreferenced since last mark and sweep.
thread_local struct list_head unreferred_objects;
thread_local bool collecting_garbage = false;

void di_init_object(di_object *obj_) {
	di_object_internal *obj = (di_object_internal *)obj_;
	obj->ref_count = 1;

	// non-zero strong references will implicitly hold a weak refrence. that reference
	// is only dropped when the object destruction finishes. this is to avoid the
	// case where the last weak reference to the object is dropped during object
	// destruction, thus cause the object to be freed in the middle of destruction.
	obj->weak_ref_count = 1;
	obj->destroyed = 0;

#ifdef TRACK_OBJECTS
	list_add(&obj->siblings, &all_objects);
#endif
	INIT_LIST_HEAD(&obj->unreferred_siblings);
}

di_object *di_new_object(size_t sz, size_t alignment) {
	if (sz < sizeof(di_object)) {
		return NULL;
	}
	if (alignment < alignof(di_object)) {
		return NULL;
	}

	di_object_internal *obj;
	DI_CHECK_OK(posix_memalign((void **)&obj, alignment, sz));
	memset(obj, 0, sz);
	di_init_object((di_object *)obj);
	return (di_object *)obj;
}

struct di_module *di_new_module_with_size(di_object *di, size_t size) {
	if (size < sizeof(struct di_module)) {
		return NULL;
	}

	struct di_module *pm = (void *)di_new_object(size, alignof(max_align_t));

	di_set_type((void *)pm, "deai:module");

	di_member_clone(pm, DEAI_MEMBER_NAME_RAW, di);

	return (void *)pm;
}

struct di_module *di_new_module(di_object *di) {
	return di_new_module_with_size(di, sizeof(struct di_module));
}

static di_variant di_remove_member_raw_impl(di_object_internal *obj, struct di_member *m) {
	HASH_DEL(*(struct di_member **)&obj->members, m);

	auto ret = (di_variant){.type = m->type, .value = m->data};
	di_free_string(m->name);
	free(m);
	return ret;
}

int di_remove_member_raw(di_object *obj, di_string name, di_variant *ret) {
	auto m = di_lookup(obj, name);
	if (!m) {
		return -ENOENT;
	}
	*ret = di_remove_member_raw_impl((void *)obj, m);
	return 0;
}

int di_delete_member_raw(di_object *obj, di_string name) {
	auto m = di_lookup(obj, name);
	if (!m) {
		return -ENOENT;
	}

	di_free_variant(di_remove_member_raw_impl((di_object_internal *)obj, (void *)m));
	return 0;
}

int di_delete_member(di_object *obj, di_string name, di_object *nullable *nullable error) {
	bool handler_found;
	int rc2 = call_handler_with_fallback(obj, "__delete", name,
	                                     (struct di_variant){NULL, DI_LAST_TYPE}, NULL,
	                                     NULL, error, &handler_found);
	if (handler_found) {
		return rc2;
	}

	return di_delete_member_raw(obj, name);
}

static void di_finalize_object_inner(di_object_internal *obj) {
#ifdef TRACK_OBJECTS
	di_log_va(log_module, DI_LOG_DEBUG, "Finalizing object %p", obj);
#endif
	// Call dtor before removing members and signals, so the dtor can still make use
	// of whatever is stored in the object, and emit more signals.
	// But this also means signal and member deleters won't be called for them.
	if (obj->dtor) {
		auto tmp = obj->dtor;
		// Never call dtor more than once
		obj->dtor = NULL;
		tmp((di_object *)obj);
	}

	struct di_member *m = (void *)obj->members;
	while (m) {
		auto next_m = m->hh.next;
#if 0
		scopedp(char) *dbg = di_value_to_string(m->type, m->data);
		fprintf(stderr, "removing member %.*s (%s)\n", (int)m->name.length,
		        m->name.data, dbg);
#endif
		di_free_variant(di_remove_member_raw_impl(obj, m));
		m = next_m;
	}
}

void di_finalize_object(di_object *_obj) {
	di_finalize_object_inner((di_object_internal *)_obj);
}

static inline void di_decrement_weak_ref_count(di_object_internal *obj) {
	obj->weak_ref_count--;
	if (obj->weak_ref_count == 0) {
		assert(obj->ref_count == 0);
#ifdef TRACK_OBJECTS
		list_del(&obj->siblings);
		fprintf(stderr, "freeing object %p\n", obj);
#endif
		free(obj);
	}
}

// Try to never call destroy twice on something. Although it's fine to do so
static void di_destroy_object(di_object *_obj) {
	auto obj = (di_object_internal *)_obj;
	assert(obj->ref_count == 0);

	if (obj->destroyed) {
		di_log_va(log_module, DI_LOG_WARN, "warning: destroy object multiple times\n");
	}
	obj->destroyed = 1;
	di_finalize_object_inner(obj);
	di_decrement_weak_ref_count(obj);
}

#if 0
static inline __attribute__((always_inline)) void print_caller(int depth) {
	unw_context_t ctx;
	unw_cursor_t cursor;
	unw_getcontext(&ctx);
	unw_init_local(&cursor, &ctx);
	for (int i = 0; i < depth && unw_step(&cursor) > 0; i++) {
		if (i) {
			printf(", ");
		}
		unw_word_t ip, off;
		unw_get_reg(&cursor, UNW_REG_IP, &ip);
		char buf[256];
		if (unw_get_proc_name(&cursor, buf, sizeof(buf), &off) == 0) {
			printf("%s+%#08lx", buf, off);
		} else {
			printf("%#08lx", ip);
		}
	}
	printf("\n");
}
#endif

di_object *di_ref_object(di_object *_obj) {
	auto obj = (di_object_internal *)_obj;
	obj->ref_count++;
#ifdef TRACK_OBJECTS
	struct di_ref_tracked_object *t = NULL;
	HASH_FIND_PTR(ref_tracked, (void **)&_obj, t);
	if (t != NULL) {
		di_log_va(log_module, DI_LOG_DEBUG, "%p is referenced (%ld)\n", _obj, obj->ref_count);
		print_stack_trace(0, 10);
	}
#endif
	return _obj;
}

struct di_weak_object *di_weakly_ref_object(di_object *_obj) {
	auto obj = (di_object_internal *)_obj;
	obj->weak_ref_count++;
	return (struct di_weak_object *)obj;
}

di_object *nullable di_upgrade_weak_ref(struct di_weak_object *weak) {
	assert(weak != PTR_POISON);
	auto obj = (di_object_internal *)weak;
	// Garbage collector cannot call user code when mark == 1
	assert(obj->mark == 0 || obj->mark == 2 || obj->mark == 3);
	// If mark == 2 or 3, the object is scheduled for destruction. If we allow
	// upgrades of weak reference to it, then user might "revive" it by, e.g. adding
	// it to roots. Yet the garbage collector will still finalize the object, causing
	// surprising behavior. So we forbid upgrades of weak references in the case.
	if (obj->ref_count > 0 && obj->destroyed == 0 && obj->mark != 2 && obj->mark != 3) {
		return di_ref_object((di_object *)obj);
	}
	return NULL;
}

void di_drop_weak_ref(struct di_weak_object **weak) {
	assert(*weak != PTR_POISON);
	auto obj = (di_object_internal *)*weak;
	di_decrement_weak_ref_count(obj);
	*weak = PTR_POISON;
}

void di_unref_object(di_object *_obj) {
	auto obj = (di_object_internal *)_obj;
	assert(obj->ref_count > 0);
	assert(!obj->destroyed);
	obj->ref_count--;
#ifdef TRACK_OBJECTS
	struct di_ref_tracked_object *t = NULL;
	HASH_FIND_PTR(ref_tracked, (void **)&_obj, t);
	if (t != NULL) {
		di_log_va(log_module, DI_LOG_DEBUG, "%p is unreferenced (%ld)\n", _obj, obj->ref_count);
		print_stack_trace(0, 10);
	}
#endif
	assert(obj->mark == 0 || obj->mark == 2 || obj->mark == 3);
	if (obj->ref_count == 0) {
		list_del_init(&obj->unreferred_siblings);
		di_destroy_object(_obj);
	} else if (list_empty(&obj->unreferred_siblings) && obj->mark == 0) {
		if (unreferred_objects.next == NULL) {
			INIT_LIST_HEAD(&unreferred_objects);
		}
		list_add(&obj->unreferred_siblings, &unreferred_objects);
	}
	// if obj->mark == 3 or 2, then the object is scheduled for destruction, we don't need
	// to add it to `unreferred_objects` list. It is unnecessary and will cause use-after-free.
}

size_t di_min_return_size(size_t in) {
	if (in < sizeof(ffi_arg)) {
		return sizeof(ffi_arg);
	}
	return in;
}

static int check_new_member(di_object_internal *obj, struct di_member *m) {
	struct di_member *om = NULL;

	if (!m->name.data) {
		return -EINVAL;
	}

	HASH_FIND(hh, obj->members, m->name.data, m->name.length, om);
	if (om) {
		return -EEXIST;
	}
	return 0;
}

static int di_insert_member(di_object_internal *obj, struct di_member *m) {
	int ret = check_new_member(obj, (void *)m);
	if (ret != 0) {
		return ret;
	}

	HASH_ADD_KEYPTR(hh, obj->members, m->name.data, m->name.length, m);
	return 0;
}

static struct di_member *
di_add_member(di_object_internal *o, di_string name, di_type t, void *v) {
	if (!name.data) {
		return ERR_PTR(-EINVAL);
	}

	auto m = tmalloc(struct di_member, 1);
	m->type = t;
	m->data = v;
	m->name = di_clone_string(name);

	int ret = di_insert_member(o, m);
	if (ret != 0) {
		di_free_value(t, v);
		free(v);

		di_free_string(m->name);
		free(m);
		return ERR_PTR(ret);
	}
	return m;
}

static struct di_member *
di_add_member_clone2(di_object *o, di_string name, di_type t, const void *value) {
	if (di_sizeof_type(t) == 0) {
		return ERR_PTR(-EINVAL);
	}

	void *copy = calloc(1, di_sizeof_type(t));
	di_copy_value(t, copy, value);

	return di_add_member((di_object_internal *)o, name, t, copy);
}

int di_add_member_clone(di_object *o, di_string name, di_type t, const void *value) {
	auto ret = di_add_member_clone2(o, name, t, value);
	if (IS_ERR(ret)) {
		return (int)PTR_ERR(ret);
	}
	return 0;
}

int di_add_member_clonev(di_object *o, di_string name, di_type t, ...) {
	if (di_sizeof_type(t) == 0) {
		return -EINVAL;
	}

	di_value nv;
	va_list ap;

	va_start(ap, t);
	va_arg_with_di_type(ap, t, &nv);
	va_end(ap);

	return di_add_member_clone(o, name, t, &nv);
}

struct di_member *di_add_member_move2(di_object *o, di_string name, di_type *t, void *addr) {
	auto sz = di_sizeof_type(*t);
	if (sz == 0) {
		return ERR_PTR(-EINVAL);
	}

	di_type tt = *t;
	void *taddr = malloc(sz);
	memcpy(taddr, addr, sz);

	*t = DI_TYPE_NIL;
	memset(addr, 0, sz);

	return di_add_member((di_object_internal *)o, name, tt, taddr);
}

int di_add_member_move(di_object *o, di_string name, di_type *t, void *addr) {
	auto ret = di_add_member_move2(o, name, t, addr);
	if (IS_ERR(ret)) {
		return (int)PTR_ERR(ret);
	}
	return 0;
}

struct di_member *di_lookup(di_object *_obj, di_string name) {
	if (name.data == NULL) {
		return NULL;
	}

	auto obj = (di_object_internal *)_obj;
	struct di_member *ret = NULL;
	HASH_FIND(hh, obj->members, name.data, name.length, ret);
	return (void *)ret;
}

di_tuple di_object_next_member(di_object *obj, di_string name) {
	di_object *next = NULL;
	if (di_rawget_borrowed(obj, "__next", next) == 0) {
		di_variant ret = DI_VARIANT_INIT;
		di_tuple args = {
		    .length = 2,
		    .elements =
		        (struct di_variant[]){
		            {.type = DI_TYPE_OBJECT, .value = (di_value *)&obj},
		            {.type = DI_TYPE_STRING, .value = (di_value *)&name},
		        },
		};
		di_value inner;
		ret.value = &inner;
		if (((di_object_internal *)next)->call(next, &ret.type, ret.value, args) != 0) {
			return DI_TUPLE_INIT;
		}
		if (ret.type != DI_TYPE_TUPLE) {
			di_free_value(ret.type, &inner);
			return DI_TUPLE_INIT;
		}
		if (inner.tuple.length != 2 || inner.tuple.elements[0].type != DI_TYPE_STRING) {
			di_free_tuple(inner.tuple);
			return DI_TUPLE_INIT;
		}
		return inner.tuple;
	}
	di_tuple ret = DI_TUPLE_INIT;
	struct di_member *m = NULL, *next_m = NULL;
	if (name.data == NULL) {
		HASH_ITER (hh, ((di_object_internal *)obj)->members, m, next_m) {
			if (di_string_starts_with(m->name, "__")) {
				continue;
			}
			break;
		}
	} else {
		m = di_lookup(obj, name);
		m = m == NULL ? NULL : m->hh.next;
		while (m != NULL && di_string_starts_with(m->name, "__")) {
			m = m->hh.next;
		}
	}
	if (m == NULL) {
		return ret;
	}
	ret.length = 2;
	ret.elements = tmalloc(struct di_variant, 2);
	ret.elements[0] = di_alloc_variant(di_clone_string(m->name));
	ret.elements[1].type = m->type;
	ret.elements[1].value = malloc(di_sizeof_type(m->type));
	di_copy_value(m->type, ret.elements[1].value, m->data);
	return ret;
}

/// Return an array of strings of all raw member names
di_array di_get_all_member_names_raw(di_object *obj_) {
	auto obj = (di_object_internal *)obj_;
	di_array ret = {
	    .arr = NULL,
	    .elem_type = DI_TYPE_STRING,
	    .length = HASH_COUNT(obj->members),
	};
	di_string *arr = ret.arr = tmalloc(di_string, ret.length);
	int cnt = 0;
	struct di_member *i, *ni;
	HASH_ITER (hh, obj->members, i, ni) {
		di_copy_value(DI_TYPE_STRING, &arr[cnt], &i->name);
		cnt += 1;
	}
	assert(cnt == ret.length);
	return ret;
}

bool di_foreach_member_raw(di_object *obj_, di_member_cb cb, void *user_data) {
	auto obj = (di_object_internal *)obj_;
	struct di_member *i, *ni;
	HASH_ITER (hh, obj->members, i, ni) {
		if (cb(i->name, i->type, i->data, user_data)) {
			return true;
		}
	}
	return false;
}

/// Restriction for destructors:
///
/// 1. They shouldn't assume any other member of `obj` is still valid.
void di_set_object_dtor(di_object *nonnull obj, di_dtor_fn nullable dtor) {
	auto internal = (di_object_internal *)obj;
	internal->dtor = dtor;
}

void di_set_object_call(di_object *nonnull obj, di_call_fn nullable call) {
	auto internal = (di_object_internal *)obj;
	internal->call = call;
}

bool di_is_object_callable(di_object *nonnull obj) {
	auto internal = (di_object_internal *)obj;
	return internal->call != NULL;
}

void di_free_tuple(di_tuple t) {
	for (int i = 0; i < t.length; i++) {
		di_free_value(DI_TYPE_VARIANT, (di_value *)&t.elements[i]);
	}
	free(t.elements);
}

void di_free_array(di_array arr) {
	if (arr.length == 0) {
		DI_CHECK(arr.arr == NULL);
		return;
	}
	size_t step = di_sizeof_type(arr.elem_type);
	for (int i = 0; i < arr.length; i++) {
		di_free_value(arr.elem_type, arr.arr + step * i);
	}
	free(arr.arr);
}

void di_free_value(di_type t, di_value *value_ptr) {
	if (t == DI_TYPE_NIL) {
		return;
	}

	// If t != DI_TYPE_UINT, then `ptr_` cannot be NULL
	di_value *nonnull val = value_ptr;
	di_object *nonnull obj;
	switch (t) {
	case DI_TYPE_ARRAY:
		di_free_array(val->array);
		break;
	case DI_TYPE_TUPLE:
		di_free_tuple(val->tuple);
		break;
	case DI_TYPE_STRING:
		di_free_string(val->string);
		break;
	case DI_TYPE_OBJECT:
	case DI_TYPE_EMPTY_OBJECT:
		obj = val->object;
		di_unref_object(obj);
		break;
	case DI_TYPE_WEAK_OBJECT:
		di_drop_weak_ref(&val->weak_object);
		break;
	case DI_TYPE_VARIANT:
		di_free_value(val->variant.type, val->variant.value);
		free(val->variant.value);
		break;
	case DI_LAST_TYPE:
	case DI_TYPE_ANY:
		DI_ASSERT(false, "Trying to free value of invalid types");
		fallthrough();
	case DI_TYPE_BOOL:
	case DI_TYPE_INT:
	case DI_TYPE_UINT:
	case DI_TYPE_NINT:
	case DI_TYPE_NUINT:
	case DI_TYPE_FLOAT:
	case DI_TYPE_POINTER:
	case DI_TYPE_STRING_LITERAL:
		// Nothing to do
		break;
	case DI_TYPE_NIL:
		// Already checked
		unreachable();
	}
}

void di_copy_value(di_type t, void *dst, const void *src) {
	const di_array *arr;
	const di_tuple *tuple;
	di_value *dstval = dst;
	const di_value *srcval = src;
	void *d;

	// dst and src only allowed to be null when t is unit
	assert(t == DI_TYPE_NIL || (dst && src));
	switch (t) {
	case DI_TYPE_ARRAY:
		arr = &srcval->array;
		if (arr->length == 0) {
			dstval->array = *arr;
		} else {
			assert(di_sizeof_type(arr->elem_type) != 0);
			d = calloc(arr->length, di_sizeof_type(arr->elem_type));
			for (int i = 0; i < arr->length; i++) {
				di_copy_value(arr->elem_type, d + di_sizeof_type(arr->elem_type) * i,
				              arr->arr + di_sizeof_type(arr->elem_type) * i);
			}
			dstval->array = (di_array){arr->length, d, arr->elem_type};
		}
		break;
	case DI_TYPE_TUPLE:
		tuple = &srcval->tuple;
		dstval->tuple.elements = tmalloc(struct di_variant, tuple->length);
		dstval->tuple.length = tuple->length;

		for (int i = 0; i < tuple->length; i++) {
			di_copy_value(DI_TYPE_VARIANT, &dstval->tuple.elements[i], &tuple->elements[i]);
		}
		break;
	case DI_TYPE_VARIANT:
		dstval->variant = (struct di_variant){
		    .type = srcval->variant.type,
		    .value = malloc(di_sizeof_type(srcval->variant.type)),
		};
		di_copy_value(srcval->variant.type, dstval->variant.value, srcval->variant.value);
		break;
	case DI_TYPE_STRING:
		dstval->string = di_clone_string(srcval->string);
		break;
	case DI_TYPE_EMPTY_OBJECT:
	case DI_TYPE_OBJECT:
		di_ref_object(srcval->object);
		dstval->object = srcval->object;
		break;
	case DI_TYPE_WEAK_OBJECT:
		dstval->weak_object = di_weakly_ref_object(srcval->object);
		break;
	case DI_TYPE_NIL:
		// nothing to do
		break;
	case DI_TYPE_ANY:
	case DI_LAST_TYPE:
		DI_PANIC("Trying to copy invalid types");
	case DI_TYPE_BOOL:
	case DI_TYPE_INT:
	case DI_TYPE_UINT:
	case DI_TYPE_NINT:
	case DI_TYPE_NUINT:
	case DI_TYPE_FLOAT:
	case DI_TYPE_POINTER:
	case DI_TYPE_STRING_LITERAL:
		memmove(dst, src, di_sizeof_type(t));
		break;
	}
}

struct di_listen_handle {
	di_object;
};

static void di_signal_remove_handler(di_object *sig_, struct di_weak_object *handler);

/// Stop the handler
///
/// EXPORT: deai:ListenHandle.stop(): :void
///
/// After calling this the signal handler will stop from being called.
static void di_listen_handle_stop(di_object *nonnull obj) {
	scoped_di_weak_object *weak_sig;
	scoped_di_weak_object *weak_handler;
	DI_CHECK_OK(di_get(obj, "weak_signal", weak_sig));
	DI_CHECK_OK(di_get(obj, "weak_handler", weak_handler));

	scoped_di_object *sig = di_upgrade_weak_ref(weak_sig);
	if (sig == NULL) {
		// Signal object already dead
		return;
	}

	di_signal_remove_handler(sig, weak_handler);
}

static void di_listen_handle_dtor(di_object *obj) {
	bool stop_on_drop;
	if (di_get(obj, "stop_on_drop", stop_on_drop) != 0) {
		stop_on_drop = false;
	}
	if (stop_on_drop) {
		di_listen_handle_stop(obj);
	}
}

/// Set whether listener should be stopped when the handle is dropped.
///
/// EXPORT: deai:ListenHandle.auto_stop(stop: :bool): :void
static void di_listen_handle_auto_stop(di_object *obj, int stop_) {
	bool stop = stop_ != 0;
	di_setx(obj, di_string_borrow_literal("stop_on_drop"), DI_TYPE_BOOL, &stop, NULL);
}

static void di_signal_remove_handler(di_object *sig_, struct di_weak_object *handler) {
	struct di_signal *sig = (void *)sig_;
	scoped_di_string handler_member_name = di_string_printf(HANDLER_PREFIX "%p", handler);

	if (di_delete_member_raw(sig_, handler_member_name) != -ENOENT) {
		sig->nhandlers -= 1;
		if (sig->nhandlers == 0) {
			// No handler remains, remove ourself from parent.
			scoped_di_weak_object *weak_source;
			scoped_di_string signal_member_name;
			DI_CHECK_OK(di_get(sig_, "weak_source", weak_source));
			DI_CHECK_OK(di_get(sig_, "signal_name", signal_member_name));
			scoped_di_object *source = di_upgrade_weak_ref(weak_source);
			if (source != NULL) {
				di_delete_member(source, signal_member_name, NULL);
			}
		}
	}
}

static void di_signal_dispatch(di_object *sig_, di_tuple args) {
	auto sig = (struct di_signal *)sig_;
	auto inner = (di_object_internal *)sig_;
	struct di_member *i, *tmpi;

	int cnt = 0;
	di_object **handlers = tmalloc(di_object *, sig->nhandlers);
	HASH_ITER (hh, inner->members, i, tmpi) {
		if (!di_string_starts_with(i->name, HANDLER_PREFIX)) {
			continue;
		}

		// Any of the handlers can be removed during emission, so first we copy
		// the list of handlers we need
		di_copy_value(DI_TYPE_OBJECT, &handlers[cnt++], i->data);
	}

	assert(cnt == sig->nhandlers);
	for (int i = 0; i < cnt; i++) {
		di_type rtype;
		di_value ret;
		scoped_di_object *err_obj = NULL;
		int rc = di_call_object_catch(handlers[i], &rtype, &ret, args, &err_obj);
		di_unref_object(handlers[i]);

		if (rc != 0) {
			di_log_va(log_module, DI_LOG_ERROR, "Failed to call a signal handler: %s\n",
			          strerror(-rc));
			continue;
		}

		di_free_value(rtype, &ret);
		if (err_obj != NULL) {
			scoped_di_string error_message = di_object_to_string(err_obj, NULL);
			di_log_va(log_module, DI_LOG_ERROR, "Error arose when calling signal handler: %.*s\n",
			          (int)error_message.length, error_message.data);
		}
	}
	free(handlers);
}

static void di_signal_add_handler(di_object *sig, di_object *handler) {
	scoped_di_string new_signal_listener_name = di_string_printf(HANDLER_PREFIX "%p", handler);
	di_add_member_clone(sig, new_signal_listener_name, DI_TYPE_OBJECT, &handler);

	((struct di_signal *)sig)->nhandlers += 1;
}

di_object *di_listen_to(di_object *_obj, di_string name, di_object *h,
                        di_object *nullable *nullable error) {
	scoped_di_string signal_member_name =
	    di_string_printf("__signal_%.*s", (int)name.length, name.data);

	auto obj = (di_object_internal *)_obj;
	assert(!obj->destroyed);

	di_object *new_error = NULL;
	di_type sig_type = DI_TYPE_NIL;
	di_value sigv;
	struct di_signal *sig = NULL;
	int rc = di_getx(_obj, signal_member_name, &sig_type, &sigv,
	                 error != NULL ? &new_error : NULL);
	assert(rc == 0 || new_error == NULL);
	if (rc == -ENOENT) {
		auto weak_source = di_weakly_ref_object(_obj);
		sig_type = DI_TYPE_NIL;
		sig = di_new_object_with_type(struct di_signal);
		sig->nhandlers = 0;
		DI_CHECK_OK(di_member(sig, "weak_source", weak_source));
		DI_CHECK_OK(di_member_clone(sig, "signal_name", signal_member_name));
		DI_CHECK_OK(di_method(sig, "remove", di_signal_remove_handler, struct di_weak_object *));
		DI_CHECK_OK(di_method(sig, "add", di_signal_add_handler, di_object *));
		DI_CHECK_OK(di_method(sig, "dispatch", di_signal_dispatch, di_tuple));
		rc = di_setx(_obj, signal_member_name, DI_TYPE_OBJECT, &sig,
		             error != NULL ? &new_error : NULL);
		if (rc != 0 && new_error == NULL) {
			new_error = di_new_error("Failed to set signal object %s", strerror(rc));
		}
	} else if (rc != 0) {
		new_error = di_new_error("Failed to get signal object %s", strerror(rc));
	} else if (rc == 0 && new_error == NULL &&
	           di_type_conversion(sig_type, &sigv, DI_TYPE_OBJECT, (di_value *)&sig, false) != 0) {
		new_error = di_new_error("Failed to signal object is not an object");
	}

	if (new_error != NULL) {
		assert(sig == NULL);
		if (error != NULL) {
			*error = new_error;
			return NULL;
		}
		di_throw(new_error);
	}

	di_signal_add_handler((di_object *)sig, h);

	auto listen_handle = di_new_object_with_type(struct di_listen_handle);
	DI_CHECK_OK(di_set_type((void *)listen_handle, "deai:ListenHandle"));

	auto weak_sig = di_weakly_ref_object((di_object *)sig);
	auto weak_handler = di_weakly_ref_object(h);
	di_member(listen_handle, "weak_signal", weak_sig);
	di_member(listen_handle, "weak_handler", weak_handler);
	di_method(listen_handle, "stop", di_listen_handle_stop);
	di_method(listen_handle, "auto_stop", di_listen_handle_auto_stop, int);

	di_set_object_dtor((void *)listen_handle, di_listen_handle_dtor);
	di_unref_object((di_object *)sig);

	return (di_object *)listen_handle;
}

static inline di_string di_object_to_string_fallback(di_object *o) {
	return di_string_printf("[object:%p]", o);
}

di_string di_object_to_string(di_object *o, di_object *nullable *nullable error) {
	// Get the __to_string member, it can be a string or a function.
	di_variant to_string;
	if (di_refrawgetx(o, di_string_borrow_literal("__to_string"), &to_string.type,
	                  &to_string.value) != 0) {
		return di_object_to_string_fallback(o);
	}

	// First, see if we can get a string out of it directly.
	di_string ret = DI_STRING_INIT;
	if (di_type_conversion(DI_TYPE_VARIANT, (di_value *)&to_string, DI_TYPE_STRING,
	                       (di_value *)&ret, true) == 0) {
		return di_clone_string(ret);
	}

	// Otherwise, it should be a function. If it's not, we fallback to the default.
	di_object *conv = NULL;
	if (di_type_conversion(DI_TYPE_VARIANT, (di_value *)&to_string, DI_TYPE_OBJECT,
	                       (di_value *)&conv, true) != 0) {
		return di_object_to_string_fallback(o);
	}

	// Call the function. If it fails, we fallback to the default.
	di_tuple args = {.length = 1,
	                 .elements = (di_variant[]){
	                     {.type = DI_TYPE_OBJECT, .value = (di_value *)&o},
	                 }};
	to_string.value = (di_value *)tmalloc(di_string, 1);
	if (error != NULL) {
		if (di_call_object_catch(conv, &to_string.type, to_string.value, args, error) != 0) {
			return di_object_to_string_fallback(o);
		}
		if (*error) {
			return DI_STRING_INIT;
		}
	} else {
		if (di_call_object(conv, &to_string.type, to_string.value, args) != 0) {
			return di_object_to_string_fallback(o);
		}
	}

	// borrowing = false, because to_string holds the return value, and therefore is owned.
	if (di_type_conversion(DI_TYPE_VARIANT, (di_value *)&to_string, DI_TYPE_STRING,
	                       (di_value *)&ret, false) != 0) {
		auto err = di_new_error("__to_string did not return a string");
		if (error != NULL) {
			*error = err;
		} else {
			di_throw(err);
		}
	}
	return ret;
}

int di_emitn(di_object *o, di_string name, di_tuple args) {
	if (args.length > MAX_NARGS) {
		return -E2BIG;
	}

	assert(args.length == 0 || (args.elements != NULL));

	scoped_di_string signal_member_name =
	    di_string_printf("__signal_%.*s", (int)name.length, name.data);
	scoped_di_object *sig = NULL;
	if (di_get2(o, signal_member_name, sig) == 0) {
		di_signal_dispatch(sig, args);
	}
	return 0;
}

#undef is_destroy

struct di_roots *roots;
di_object *di_get_roots(void) {
	return (di_object *)roots;
}

static void di_scan_type(di_type type, di_value *value, int (*pre)(di_object_internal *, int),
                         int state, void (*post)(di_object_internal *)) {
	if (type == DI_TYPE_OBJECT) {
		auto obj = (di_object_internal *)value->object;
		int next_state = 0;
		if (pre) {
			next_state = pre(obj, state);
		}
		if (next_state >= 0) {
			struct di_member *m, *tmpm;
			HASH_ITER (hh, obj->members, m, tmpm) {
				di_scan_type(m->type, m->data, pre, next_state, post);
			}
			if (post) {
				post(obj);
			}
		}
	} else if (type == DI_TYPE_ARRAY) {
		if (value->array.length == 0) {
			// elem_type could be an invalid type
			return;
		}
		auto step = di_sizeof_type(value->array.elem_type);
		for (int i = 0; i < value->array.length; i++) {
			di_scan_type(value->array.elem_type, value->array.arr + step * i, pre, state, post);
		}
	} else if (type == DI_TYPE_TUPLE) {
		for (int i = 0; i < value->tuple.length; i++) {
			di_scan_type(value->tuple.elements[i].type, value->tuple.elements[i].value,
			             pre, state, post);
		}
	} else if (type == DI_TYPE_VARIANT) {
		di_scan_type(value->variant.type, value->variant.value, pre, state, post);
	}
}

// Note about debug logging: we can call into the log module during garbage collection,
// because it will ref/unref objects, which could mess things up. So for `TRACK_OBJECTS`
// we use `fprintf(stderr, ...)` directly.

// Stage 1, count all references reachable from the unreferenced root object. mark is set to 1
static int di_collect_garbage_mark(di_object_internal *o, int _ unused) {
#ifdef TRACK_OBJECTS
	fprintf(stderr, "\tmark %p %s %lu/%lu\n", o, di_get_type((void *)o),
	        o->ref_count_scan, o->ref_count);
#endif
	list_del_init(&o->unreferred_siblings);
	o->ref_count_scan += 1;
	if (o->mark == 1) {
		return -1;
	}
	o->mark = 1;
	return 0;
}

// Stage 2, check if ref_count_scan == ref_count. If true, this means all references to
// this object are reachable from the unreferenced root object, i.e. there is no external
// references, meaning this object can be garbage collected.
//
// If ref_count_scan < ref_count, this means there are external references to this object,
// we cannot garbage collect it, and we also need to revive all objects that are reachable
// from this object.
//
// mark is set to 2 if this object should be collected, and to 0 if this object is
// revived.
static int di_collect_garbage_scan(di_object_internal *o, int revive) {
	if (o->ref_count_scan > o->ref_count) {
		fprintf(stderr, "Object %p %s %lu/%lu reference count is too low", o,
		        di_get_type((void *)o), o->ref_count_scan, o->ref_count);
#ifdef TRACK_OBJECTS
		di_dump_objects();
#endif
		assert(false);
	}
	if (revive || o->ref_count_scan != o->ref_count) {
		if (o->mark == 0) {
			return -1;
		}
#ifdef TRACK_OBJECTS
		fprintf(stderr, "\treviving %p, %lu/%lu %s\n", o, o->ref_count_scan, o->ref_count,
		        di_get_type((void *)o));
#endif
		o->ref_count_scan = 0;
		o->mark = 0;
		return 1;
	}
	if (o->mark == 2 || o->mark == 0) {
		return -1;
	}
	o->mark = 2;
	// all references to `o` are internal, so this object can be collected.
#ifdef TRACK_OBJECTS
	fprintf(stderr, "\tcollecting %p, %lu/%lu %s\n", o, o->ref_count_scan, o->ref_count,
	        di_get_type((void *)o));
#endif
	return 0;
}
static int di_collect_garbage_collect_pre(di_object_internal *o, int _ unused) {
	if (o->mark == 0 || o->mark == 3) {
		return -1;
	}
	// Make sure objects will not be finalized before we manually finalize them.
	// fprintf(stderr, "\tpre-finalizing %p, %lu\n", o, o->ref_count);
	di_ref_object((void *)o);
	o->mark = 3;
	return 0;
}
static void di_collect_garbage_collect_post(di_object_internal *o) {
	assert(o->mark == 3);
	// fprintf(stderr, "\tpost-finalizing %p, %lu\n", o, o->ref_count);
	di_finalize_object_inner(o);
	di_unref_object((void *)o);
}
/// Collect garbage in cyclic references
void di_collect_garbage(void) {
	while (!list_empty(&unreferred_objects)) {
		di_object_internal *i, *ni;
		// While we scan, we need to remove roots that can be reached from other
		// roots, so in the end we guarantee that no roots can reach each other.
		// This is an important assumption we need when we finalize objects.

		// Move scanned objects to a new list, so if finalizing some
		// of the objects here cause some other objects to be unreferenced, they
		// could be added to the list without disrupting the collection.
		struct list_head isolated_roots;
		INIT_LIST_HEAD(&isolated_roots);
		while (!list_empty(&unreferred_objects)) {
			i = list_first_entry(&unreferred_objects, di_object_internal, unreferred_siblings);
			// fprintf(stderr, "unref root: %p %lu/%lu %d, %s\n", i, i->ref_count_scan,
			//        i->ref_count, i->mark, di_get_type((void *)i));

			// di_collect_garbage_mark will remove all reached objects from unreferred_objects.
			assert(i->mark == 0);
			di_scan_type(DI_TYPE_OBJECT, (void *)&i, di_collect_garbage_mark, 0, NULL);
			// unreferred_objects list doesn't constitute as a reference.
			i->ref_count_scan -= 1;
			list_add(&i->unreferred_siblings, &isolated_roots);
		}

		list_for_each_entry (i, &isolated_roots, unreferred_siblings) {
			// fprintf(stderr, "unref root: %p %lu/%lu %d, %s\n", i, i->ref_count_scan,
			//        i->ref_count, i->mark, di_get_type((void *)i));
			di_scan_type(DI_TYPE_OBJECT, (void *)&i, di_collect_garbage_scan, 0, NULL);
		}

		// First, remove all revived objects from the list. Because unreferring
		// them will add them to the `unreferred_objects` list. If we don't remove
		// them from this list first, this list will be corrupted.
		list_for_each_entry_safe (i, ni, &isolated_roots, unreferred_siblings) {
			assert(i->mark == 0 || i->mark == 2);
			if (i->mark != 2) {
				list_del_init(&i->unreferred_siblings);
			}
		}

		while (!list_empty(&isolated_roots)) {
			// fprintf(stderr, "unref root: %p %lu/%lu %d\n", i,
			//         i->ref_count_scan, i->ref_count, i->mark);
			i = list_first_entry(&isolated_roots, di_object_internal, unreferred_siblings);
			di_scan_type(DI_TYPE_OBJECT, (void *)&i, di_collect_garbage_collect_pre, 0,
			             di_collect_garbage_collect_post);

			// `i` should have been freed at this point. and the last unref should have removed it from to_finalize.
		}
	}
}

bool di_is_empty_object(di_object *nonnull obj) {
	return ((di_object_internal *)obj)->members == NULL;
}

#ifdef TRACK_OBJECTS
void di_track_object_ref(di_object *unused obj, void *pointer) {
	auto t = tmalloc(struct di_ref_tracked_object, 1);
	t->ptr = pointer;
	HASH_ADD_PTR(ref_tracked, ptr, t);
}
static char *indent(int level) {
	char *ret = malloc(2 * level + 1);
	memset(ret, ' ', 2 * level);
	ret[2 * level] = '\0';
	return ret;
}

static void di_dump_array(di_array arr, int depth);
static void di_dump_tuple(di_tuple t, int depth) {
	if (t.length == 0) {
		return;
	}
	scopedp(char) *prefix = indent(depth);
	di_log_va(log_module, DI_LOG_DEBUG, "\t%s(", prefix);
	for (int i = 0; i < t.length; i++) {
		scopedp(char) *value_string =
		    di_value_to_string(t.elements[i].type, t.elements[i].value);
		di_log_va(log_module, DI_LOG_DEBUG, "\t%s  %s: %s,", prefix,
		          di_type_to_string(t.elements[i].type), value_string);
		if (t.elements[i].type == DI_TYPE_OBJECT) {
			((di_object_internal *)t.elements[i].value->object)->ref_count_scan--;
		} else if (t.elements[i].type == DI_TYPE_ARRAY) {
			di_dump_array(t.elements[i].value->array, depth + 1);
		} else if (t.elements[i].type == DI_TYPE_TUPLE) {
			di_dump_tuple(t.elements[i].value->tuple, depth + 1);
		}
	}
	di_log_va(log_module, DI_LOG_DEBUG, "\t%s)", prefix);
}
static void di_dump_array(di_array arr, int depth) {
	if (arr.length == 0) {
		return;
	}
	scopedp(char) *prefix = indent(depth);
	auto step_size = di_sizeof_type(arr.elem_type);
	di_log_va(log_module, DI_LOG_DEBUG, "\t%s[", prefix);
	for (int i = 0; i < arr.length; i++) {
		void *curr = arr.arr + i * step_size;
		scopedp(char) *value_string = di_value_to_string(arr.elem_type, curr);
		di_log_va(log_module, DI_LOG_DEBUG, "\t%s  %s,", prefix, value_string);
		if (arr.elem_type == DI_TYPE_OBJECT) {
			((di_object_internal *)curr)->ref_count_scan--;
		} else if (arr.elem_type == DI_TYPE_ARRAY) {
			di_dump_array(*(di_array *)curr, depth + 1);
		} else if (arr.elem_type == DI_TYPE_TUPLE) {
			di_dump_tuple(*(di_tuple *)curr, depth + 1);
		}
	}
	di_log_va(log_module, DI_LOG_DEBUG, "\t%s]", prefix);
}
static void di_dump_type_content(di_type type, di_value *value) {
	if (type == DI_TYPE_OBJECT) {
		auto obj_internal = (di_object_internal *)value->object;
		obj_internal->ref_count_scan--;
	} else if (type == DI_TYPE_ARRAY) {
		di_dump_array(value->array, 0);
	} else if (type == DI_TYPE_TUPLE) {
		di_dump_tuple(value->tuple, 0);
	} else if (type == DI_TYPE_VARIANT) {
		di_dump_type_content(value->variant.type, value->variant.value);
	}
}
void di_dump_object(di_object *obj_) {
	auto obj = (di_object_internal *)obj_;
	di_log_va(log_module, DI_LOG_DEBUG,
	          "%p, ref count: %lu strong %lu weak (live: %d), type: %s\n", obj,
	          obj->ref_count, obj->weak_ref_count, obj->mark, di_get_type((void *)obj));
	struct di_member *m, *tmpm;
	HASH_ITER (hh, obj->members, m, tmpm) {
		char *value_string = di_value_to_string(m->type, m->data);
		di_log_va(log_module, DI_LOG_DEBUG, "\tmember: %.*s, type: %s (%s)",
		          (int)m->name.length, m->name.data, di_type_to_string(m->type), value_string);
		free(value_string);
		di_dump_type_content(m->type, m->data);
	}
}
void di_dump_objects(void) {
	di_object_internal *i;
	list_for_each_entry (i, &all_objects, siblings) {
		i->ref_count_scan = i->ref_count;
	}

	list_for_each_entry (i, &all_objects, siblings) {
		di_dump_object((di_object *)i);
	}

	// Account for references from the roots
	if (roots != NULL) {
		roots->ref_count_scan--;
		di_log_va(log_module, DI_LOG_DEBUG, "Anonymous roots:\n");
		for (auto root = roots->anonymous_roots; root; root = root->hh.next) {
			auto obj_internal = (di_object_internal *)root->obj;
			obj_internal->ref_count_scan--;
			di_log_va(log_module, DI_LOG_DEBUG, "\t%p\n", root->obj);
		}
	}

	di_log_va(log_module, DI_LOG_DEBUG, "Reference count diagnostics:\n");
	list_for_each_entry (i, &all_objects, siblings) {
		const char *color = "";
		if (i->ref_count_scan > 0) {
			color = "\033[31;1m";
		}
		di_log_va(log_module, DI_LOG_DEBUG, "%s%p, excess_ref_count: %lu/%lu\033[0m\n",
		          color, i, i->ref_count_scan, i->ref_count);
		i->ref_count_scan = 0;
	}
}

#endif

const char *di_type_names[] = {
    [DI_TYPE_NIL] = "nil",
    [DI_TYPE_ANY] = "any",
    [DI_TYPE_EMPTY_OBJECT] = "empty_object",
    [DI_TYPE_BOOL] = "bool",
    [DI_TYPE_NINT] = "nint",
    [DI_TYPE_NUINT] = "nuint",
    [DI_TYPE_INT] = "int",
    [DI_TYPE_UINT] = "uint",
    [DI_TYPE_FLOAT] = "float",
    [DI_TYPE_POINTER] = "pointer",
    [DI_TYPE_OBJECT] = "object",
    [DI_TYPE_WEAK_OBJECT] = "weak_object",
    [DI_TYPE_STRING] = "string",
    [DI_TYPE_STRING_LITERAL] = "string_literal",
    [DI_TYPE_ARRAY] = "array",
    [DI_TYPE_TUPLE] = "tuple",
    [DI_TYPE_VARIANT] = "variant",
    [DI_LAST_TYPE] = "invalid",
};
