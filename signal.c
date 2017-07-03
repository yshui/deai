#include <stdarg.h>

#include <deai/helper.h>
#include <deai/signal.h>

#include "di_internal.h"
#include "uthash.h"
#include "utils.h"

struct di_signal_internal {
	struct di_object;

	int nargs;
	int (*new)(struct di_signal *);
	void (*remove)(struct di_signal *);
	struct list_head listeners;

	di_type_t types[];
};

struct di_listener {
	struct di_object;

	struct di_object *emitter;
	struct di_object *handler;
	struct di_signal *signal;
	struct list_head siblings;
};

static void signal_dtor(struct di_signal_internal *sig) {
	// Remove all listener
	if (!list_empty(&sig->listeners) && sig->remove)
		sig->remove((void *)sig);

	struct di_listener *l, *ln;
	list_for_each_entry_safe(l, ln, &sig->listeners, siblings) {
		list_del(&l->siblings);
		di_unref_object(l->handler);
		free(l);
	}
}

PUBLIC struct di_signal *di_new_signal(int nargs, di_type_t *types) {
	struct di_signal_internal *sig = (void *)di_new_object(
	    sizeof(struct di_signal_internal) + sizeof(di_type_t) * (nargs + 1));

	sig->nargs = nargs;
	INIT_LIST_HEAD(&sig->listeners);

	if (nargs)
		memcpy(sig->types + 1, types, sizeof(di_type_t) * nargs);
	sig->types[0] = DI_TYPE_OBJECT;

	sig->dtor = (void *)signal_dtor;
	ABRT_IF_ERR(di_set_type((void *)sig, "signal"));

	return (void *)sig;
}

PUBLIC struct di_listener *di_new_listener(void) {
	struct di_listener *l = di_new_object_with_type(struct di_listener);

	di_add_address_member((void *)l, "emitter", true, DI_TYPE_OBJECT, &l->emitter);
	di_add_address_member((void *)l, "handler", true, DI_TYPE_OBJECT, &l->handler);

	di_method(l, "stop", di_stop_listener);

	ABRT_IF_ERR(di_set_type((void *)l, "listener"));
	return l;
}

PUBLIC struct di_listener *
di_add_listener_to_signal(struct di_signal *_sig, struct di_object *h) {
	struct di_signal_internal *sig = (void *)_sig;

	if (list_empty(&sig->listeners)) {
		if (sig->new) {
			int ret = sig->new ((void *)sig);
			if (ret != 0)
				return ERR_PTR(ret);
		}
	}

	struct di_listener *l = tmalloc(struct di_listener, 1);
	l->handler = h;
	l->signal = _sig;

	di_ref_object((void *)sig);
	di_ref_object(h);

	list_add(&l->siblings, &sig->listeners);

	di_ref_object((void *)l);

	return l;
}

PUBLIC int
di_remove_listener_from_signal(struct di_signal *_sig, struct di_listener *l) {
	struct di_signal_internal *sig = (void *)_sig;
	struct di_listener *p = NULL;
	list_for_each_entry(p, &sig->listeners, siblings) if (p == l) goto del;
	return -ENOENT;

del:
	list_del(&p->siblings);

	if (list_empty(&sig->listeners)) {
		if (sig->remove)
			sig->remove((void *)sig);
	}

	if (p->emitter)
		di_unref_object(p->emitter);

	di_unref_object((void *)p->signal);
	di_unref_object(p->handler);
	di_unref_object((void *)p);
	return 0;
}

PUBLIC int
di_stop_listener(struct di_listener *l) {
	return di_remove_listener_from_signal(l->signal, l);
}

PUBLIC void di_bind_listener(struct di_listener *l, struct di_object *e) {
	if (l->emitter)
		di_unref_object(l->emitter);
	l->emitter = e;
	di_ref_object(e);
}

PUBLIC int di_emitv(struct di_signal *_sig, struct di_object *emitter, va_list ap) {
	if (!emitter)
		return -EINVAL;

	struct di_signal_internal *sig = (void *)_sig;

	void **args = alloca(sizeof(void *) * (sig->nargs + 1));
	for (unsigned int i = 1; i < sig->nargs; i++) {
		assert(di_sizeof_type(sig->types[i]) != 0);
		args[i] = alloca(di_sizeof_type(sig->types[i]));
		va_arg_with_di_type(ap, sig->types[i], args[i]);
	}

	struct di_listener *l, *nl;

	// Hold a reference to prevent object from being freed during
	// signal emission
	assert(emitter->ref_count > 0);
	di_ref_object(emitter);

	args[0] = emitter;

	// Allow remove listener from listener
	list_for_each_entry_safe(l, nl, &sig->listeners, siblings) {
		di_type_t rtype = DI_TYPE_NIL;
		void *ret = NULL;
		if (l->emitter && emitter != l->emitter)
			continue;

		l->handler->call(l->handler, &rtype, &ret, sig->nargs + 1,
		                 sig->types, (const void *const *)args);

		di_free_value(rtype, (void *)ret);
		free((void *)ret);
	}

	di_unref_object(emitter);
	return 0;
}

PUBLIC int di_emit(struct di_signal *sig, struct di_object *emitter, ...) {
	va_list ap;
	va_start(ap, emitter);

	int ret = di_emitv(sig, emitter, ap);
	va_end(ap);
	return ret;
}
