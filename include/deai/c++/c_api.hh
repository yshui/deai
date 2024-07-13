// SPDX-License-Identifier: MPL-2.0

#pragma once
#include <cstdlib>

namespace deai::c_api {
extern "C" {
#define __auto_type auto        // NOLINT
#include "../callable.h"
#include "../error.h"
#include "../object.h"
#undef __auto_type
}
}        // namespace deai::c_api
