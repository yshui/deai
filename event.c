#include <errno.h>
#include <assert.h>
#include "piped_internal.h"
#include "utils.h"

int piped_event_source_add_listener(struct piped_evsrc *e, const char *ev_name,
				    struct piped_fn *f) {
	struct piped_listener *l = tmalloc(struct piped_listener, 1);
	if (!l)
		return -ENOMEM;

	l->f = (void *)f;

	struct piped_evsrc_sub *sub = NULL;
	HASH_FIND_STR(e->sub, ev_name, sub);
	if (!sub) {
		sub = tmalloc(struct piped_evsrc_sub, 1);
		sub->name = strdup(ev_name);
		INIT_LIST_HEAD(&sub->listeners);
		HASH_ADD_KEYPTR(hh, e->sub, sub->name, strlen(sub->name), sub);
	}
	list_add(&l->siblings, &sub->listeners);
	return 0;
}

int piped_event_source_emit(struct piped_evsrc *e, const struct piped_event_desc *evd,
			    void **ev_data) {
	struct piped_evsrc_sub *sub = NULL;
	HASH_FIND_STR(e->sub, evd->name, sub);
	if (!sub)
		return 0;

	struct piped_listener *l;
	void **tmp_ev_data = tmalloc(void *, evd->nargs);
	list_for_each_entry(l, &sub->listeners, siblings) {
		memcpy(tmp_ev_data, ev_data, sizeof(void *)*evd->nargs);
		ffi_call(&l->f->cif, l->f->fn_ptr, NULL, tmp_ev_data);
	}
	return 0;
}
