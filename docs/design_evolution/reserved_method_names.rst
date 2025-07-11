=====================
Reserved method names
=====================

Several special method names is reserved by **deai** for specific purposes, here is a list of them (when not mentioned, the value should be a callable ``di_object``):

* ``__del_signal``: called when the last listener is detached from some signal.
* ``__new_signal``: called when the first listener is attached to a non-existent signal, if the signal is still non-existent after this function returns, add listener will fail.
* ``__get_<prop>``, ``__set_<prop>``: getter and setter functions for property ``prop``.
* ``__geti``, ``__seti``: **deai** objects properties can only have string keys. these are the getter/setter functions for integer keys.
* ``__max_index_hint``: a hint for what's the maximum index ``__geti``/``__seti`` might accept. it is allowed to be wrong in either direction. can be either a callable object, or a number.
* ``__get``, ``__set``: general getter/setter, takes a string as first argument for the field name. only called when specific getter setter doesn't exist. ``__set`` can optionally return a ``DI_TYPE_OBJECT`` to indicate errors.
* ``__error_msg``: a string, exist iff the object represents an error.
* ``__properties``: all possible property names one can pass to ``__get``/``__set``. can be either a callable object that returns an array of strings, or an array of strings.
* ``__detach``: If such method presents in a listener object, it will be called when the listener stops because of the dying of the object listened to.

=====================
Reserved signal names
=====================

* ``__destroyed``: A stub signal that is never emitted. Listen to this signal won't prevent the object from being freed. The ``__detach`` function of such listeners can be used to monitor the destruction of listened object,
  useful for implementing weak ref.
