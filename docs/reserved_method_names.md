# Reserved method names

Several special method names is reserved by **deai** for specific purposes, here is a list of them:

* __dtor(): called when an object is being freed, takes no argument.
* __add_listener(const char *): called when a new listener is attached to an object.
* __get_<fieldname>, __set_<fieldname>: getter and setter functions for field `fieldname`.
