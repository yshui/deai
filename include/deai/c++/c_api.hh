// SPDX-License-Identifier: MPL-2.0

#pragma once
#include <cstdlib>        // IWYU pragma: keep
extern "C" {
#define __auto_type auto        // NOLINT
#include "../callable.h"
#include "../error.h"
#include "../object.h"
#undef __auto_type
}

namespace deai::c_api {
using Type = ::di_type;
using Variant = ::di_variant;
using Tuple = ::di_tuple;
using Array = ::di_array;
using Value = ::di_value;
using String = ::di_string;
using Object = ::di_object;
using WeakObject = ::di_weak_object;

namespace type {
constexpr auto sizeof_ = ::di_sizeof_type;
constexpr auto check = ::di_check_type;
inline constexpr auto &names = ::di_type_names;
}        // namespace type
namespace object {
constexpr auto new_ = ::di_new_object;
constexpr auto new_with_type_name = ::di_new_object_with_type_name;
constexpr auto new_error = ::di_new_error;
constexpr auto ref = ::di_ref_object;
constexpr auto weakly_ref = ::di_weakly_ref_object;
constexpr auto unref = ::di_unref_object;
constexpr auto add_member_move = ::di_add_member_move;
constexpr auto add_member_clone = ::di_add_member_clone;
constexpr auto set_call = ::di_set_object_call;
constexpr auto set_dtor = ::di_set_object_dtor;
}        // namespace object
namespace string {
constexpr auto borrow = ::di_string_borrow;
constexpr auto ndup = ::di_string_ndup;
constexpr auto dup = ::di_string_dup;
}        // namespace string
namespace weak_object {
constexpr auto upgrade = ::di_upgrade_weak_ref;
constexpr auto drop = ::di_drop_weak_ref;
}        // namespace weak_object
}        // namespace deai::c_api
