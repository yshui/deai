# Reserved method names

Several special method names is reserved by **deai** for specific purposes, here is a list of them:

* __dtor(): called when an object is being freed, takes no argument.
* __add_listener_<signalname>: called when the first listener is attached to <signalname>
* __del_listener_<signalname>: called when the last listener is detached from <signalname>
* __add_listener(const char *): called when the first listener is attached to a non-existent signal, if the signal is still non-existent after this function returns, add listener will fail.
* __get_<fieldname>, __set_<fieldname>: getter and setter functions for field `fieldname`.
* __get, __set: general getter/setter, takes a string as first argument for the field name. only called when specific getter setter doesn't exist
* __error_msg: Exist iff the object represents an error
