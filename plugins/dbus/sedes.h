#pragma once
#include <deai/deai.h>
#include <dbus/dbus.h>
void _dbus_deserialize_tuple(DBusMessageIter *i, void *retp);
int _dbus_serialize_tuple(DBusMessageIter *i, struct di_tuple);
