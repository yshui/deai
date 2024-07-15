#include <exception>
#include <deai/c++/deai.hh>

extern "C" {
void noret di_throw(di_object *obj) {
	throw obj;
}

auto di_try(void (*func)(void *), void *args) -> di_object * {
	try {
		func(args);
	} catch (di_object *&err) {
		return err;
	} catch (std::exception &e) {
		return ::di_new_error("Other C++ exceptions: %s", e.what());
	}
	return nullptr;
}
}
