=====================
Reserved method names
=====================

Several special method names is reserved by **deai** for specific purposes, here is a list of them:

* ``__del_signal``: called when the last listener is detached from some signal.
* ``__new_signal``: called when the first listener is attached to a non-existent signal, if the signal is still non-existent after this function returns, add listener will fail.
* ``__get_<prop>``, ``__set_<prop>``: getter and setter functions for property ``prop``.
* ``__get``, ``__set``: general getter/setter, takes a string as first argument for the field name. only called when specific getter setter doesn't exist. ``__set`` can optionally return a ``DI_TYPE_OBJECT`` to indicate errors.
* ``__error_msg``: Exist iff the object represents an error
* ``__properties``: Return array of strings represents possible property names one can pass to ``__get``/``__set``.

=====================
Reserved signal names
=====================

* ``__destroyed``: A special signal emitted right before the dtor is called. Listen to this signal won't prevent the object from being freed, useful for implementing weak ref.
  Listeners would be cleared before di_emitn returns. User can manually emit this signal, but it has to be emitted via a strong ref.
  If the __destroyed handler is the only way to kill your object, you don't need to keep a ref to the listener and stop it in your dtor. Because there'll be no listeners left after this signal.
  If, however, your object might die in other ways (e.g. losing all ref, or having a "destroy-er" method), the first thing you need to do when killing the object, is to stop the ``__destroyed`` listener.
