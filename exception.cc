extern "C" {
#include <deai/deai.h>
void noret di_throw(di_object *obj) {
	throw obj;
}

auto di_try(void (*func)(void *), void *args) -> di_object * {
	try {
		func(args);
	} catch (di_object *&err) {
		return err;
	}
	return nullptr;
}
}
