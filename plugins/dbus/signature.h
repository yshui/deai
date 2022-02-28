#pragma once

#include <stdlib.h>
#include <deai/object.h>

struct dbus_signature {
	struct di_string current;
	int nchild;
	struct dbus_signature *child;
};

static inline void free_dbus_signature(struct dbus_signature sig) {
	for (int i = 0; i < sig.nchild; i++) {
		free_dbus_signature(sig.child[i]);
	}
	free(sig.child);
}

int di_type_to_dbus_basic(di_type_t type);
struct dbus_signature type_signature_of_di_value(struct di_variant var);
struct dbus_signature parse_dbus_signature(struct di_string signature);
