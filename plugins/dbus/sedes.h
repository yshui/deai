#pragma once
#include <deai/deai.h>
#include <dbus/dbus.h>
void dbus_deserialize_struct(DBusMessageIter *i, void *retp);

/// Serialize a di_array as dbus struct
int dbus_serialize_struct(DBusMessageIter *i, di_tuple, di_string signature);
