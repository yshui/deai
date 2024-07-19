#include <deai/c++/deai.hh>
#include <exception>

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
		return deai::util::new_error(std::format("Other C++ exceptions: %s", e.what()));
	}
	return nullptr;
}

auto di_call_object_catch(di_object *obj, di_type *rt, di_value *ret, di_tuple args,
                          di_object **err) -> int {
	*err = nullptr;
	try {
		return di_call_object(obj, rt, ret, args);
	} catch (di_object *&e) {
		*err = e;
		*rt = deai::c_api::Type::NIL;
	} catch (std::exception &e) {
		*err = deai::util::new_error(std::format("Other C++ exceptions: %s", e.what()));
		*rt = deai::c_api::Type::NIL;
	}
	return 0;
}
}
