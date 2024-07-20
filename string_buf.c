/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <string.h>
#include <stdlib.h>
#include "common.h"

#include <deai/helper.h>
#include "string_buf.h"

struct string_buf {
	struct string_buf_node *head;
	struct string_buf_node **tail;
};

struct string_buf_node {
	char *str;
	size_t len;
	struct string_buf_node *next;
};

static inline char *strldup(const char *src, size_t *len) {
	*len = strlen(src);
	return strdup(src);
}

void string_buf_push(struct string_buf *buf, const char *str) {
	auto n = tmalloc(struct string_buf_node, 1);
	n->str = strldup(str, &n->len);

	*buf->tail = n;
	buf->tail = &n->next;
}

void string_buf_lpush(struct string_buf *buf, const char *str, size_t len) {
	auto n = tmalloc(struct string_buf_node, 1);
	n->str = strndup(str, len);
	n->len = len;

	*buf->tail = n;
	buf->tail = &n->next;
}

bool string_buf_is_empty(struct string_buf *buf) {
	return !buf->head;
}

void string_buf_clear(struct string_buf *buf) {
	auto tmp = buf->head;
	while(tmp) {
		auto next = tmp->next;
		free(tmp->str);
		free(tmp);
		tmp = next;
	}
	buf->head = NULL;
	buf->tail = &buf->head;
}

char *string_buf_dump(struct string_buf *buf) {
	size_t len = 0;

	struct string_buf_node *tmp = buf->head;
	while(tmp) {
		len += strlen(tmp->str);
		tmp = tmp->next;
	}

	char *ret = malloc(len+1);
	char *pos = ret;
	tmp = buf->head;
	while(tmp) {
		memcpy(pos, tmp->str, tmp->len);
		pos += tmp->len;
		auto next = tmp->next;
		free(tmp->str);
		free(tmp);
		tmp = next;
	}

	buf->head = NULL;
	buf->tail = &buf->head;
	*pos = 0;
	return ret;
}


struct string_buf *string_buf_new(void) {
	auto ret = tmalloc(struct string_buf, 1);
	ret->tail = &ret->head;
	return ret;
}
