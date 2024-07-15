// SPDX-License-Identifier: MPL-2.0

/* Copyright (c) 2022, Yuxuan Shui <yshuiv7@gmail.com> */
#pragma once
#include "object.h"

/// Convert value `from` of type `from_type` to type `to_type`. The conversion operates in 2 mode:
///
///   - If `from`'s value is owned, it will be destructed during conversion, the ownership
///     will be transferred to `to`. There is no need to free `from` afterwards, but `to`
///     is expected to be freed by the caller.
///
///   - If `from`'s value is not owned, the conversion will not touch `from`, and `to`
///     will borrow the value in `from` if needed. `to` must not be freed afterwards.
///
/// Returns 0 on success, -EINVAL if the conversion is not possible, -ERANGE if source is
/// a integer type and its value is out of range of the destination type. If the
/// conversion fails, `to` is left untouched.
///
/// @param[in] borrowing Whether the `from` value is owned or borrowed. Some conversion can
//                       not be performed if the caller owns the value, as that would
//                       cause memory leakage. If `from` is borrowed, `to` must also be
//                       borrowed downstream as well. If `borrowing` is false and conversion
//                       is failed, `from` will be freed.
int di_type_conversion(di_type from_type, di_value *from, di_type to_type, di_value *to,
                       bool borrowing);
void di_int_conversion(di_type from_type, di_value *from, int to_bits, bool to_unsigned,
                       void *to);
