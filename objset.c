/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/deai.h>
#include <deai/helper.h>
#include <deai/objset.h>

#include "common.h"
#include "uthash.h"

struct di_objset_member {
	struct di_object *obj;
	UT_hash_handle hh;
};

struct di_objset {
	struct di_object;
	struct di_objset_member *set_members;
};

PUBLIC int di_hold_object(struct di_objset *set, struct di_object *obj) {
	struct di_objset_member *m = NULL;
	HASH_FIND_PTR(set->set_members, &obj, m);
	if (m)
		return -EEXIST;

	m = tmalloc(struct di_objset_member, 1);
	m->obj = obj;
	HASH_ADD_PTR(set->set_members, obj, m);

	di_ref_object(obj);
	// notify the object
	di_call(obj, "objset_hold", (struct di_object *)set);
	return 0;
}

PUBLIC int di_release_object(struct di_objset *set, const struct di_object *obj) {
	struct di_objset_member *m = NULL;
	HASH_FIND_PTR(set->set_members, &obj, m);
	if (!m)
		return -ENOENT;

	HASH_DEL(set->set_members, m);

	di_call(m->obj, "objset_release");
	di_unref_object(m->obj);
	free(m);
	return 0;
}

PUBLIC void di_release_all_objects(struct di_objset *set) {
	struct di_objset_member *m, *nm;

	HASH_ITER(hh, set->set_members, m, nm) {
		HASH_DEL(set->set_members, m);
		di_call(m->obj, "objset_release");
		di_unref_object(m->obj);
		free(m);
	}
}

PUBLIC struct di_objset *di_new_objset(void) {
	auto set = di_new_object_with_type(struct di_objset);

	di_method(set, "hold", di_hold_object, struct di_object *);
	di_method(set, "release", di_release_object, struct di_object *);
	di_method(set, "clear", di_release_all_objects);

	set->dtor = (void *)di_release_all_objects;

	return set;
}
