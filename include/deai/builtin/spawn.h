/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include <deai/object.h>

struct di_spawn;

/**
 * Spawn a child process with arguments
 *
 * @param[in] s The spawn module object
 * @param[in] argv The arguments passed to exec
 * @param[in] ignore_output If true, the stdout and stderr will be redirected to
 *            /dev/null
 */
struct di_object *
di_spawn_run(struct di_spawn *s, struct di_array argv, bool ignore_output);
