
#ifndef MESSAGE_H__
#define MESSAGE_H__

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MESSAGE_MAX 30
#define MESSAGE_TIMELIMIT 1000
#define MAX_MESSAGES 5

char __attribute__((__common__)) *_queue[MAX_MESSAGES];
char __attribute__((__common__)) _buf[MESSAGE_MAX];

struct message_t {
	void	(*add)(const char *format, ...);
	char*	(*get)();
	void	(*clear)(char *format, ...);
	void	(*clear_all)();
	uint32_t	timestamp;
	uint32_t	timelimit;
};

static void add(const char *format, ...) {
	int j;
	va_list va;
	va_start(va, format);
	vsprintf(_buf, format, va);
	va_end(va);
	for (int i = 0; i < MAX_MESSAGES; i++)
		if (_queue[i] && !strcmp(_buf, _queue[i]))
			return;
	for (int i = 0; i < MAX_MESSAGES; i++)
		if (!_queue[i])
			j = i;
	if (asprintf(&_queue[ 0 + j ], "%s", _buf) == -1)
		fprintf(stderr, "Unable to add %s, asprintf() failed, out of memory\n", _buf);
}

static char* get() {
	for (int i = 0; i < MAX_MESSAGES; i++)
		if (_queue[i])
			return _queue[i];
	return 0;
}

static void clear(char *format, ...) {
	va_list va;
	va_start(va, format);
	vsprintf(_buf, format, va);
	va_end(va);
	for (int i = 0; i < MAX_MESSAGES; i++)
		if (_queue[i] && !strcmp(_buf, _queue[i])) {
			_queue[i] = 0;
			return;
		}
}

static void clear_all() {
	for (int i = 0; i < MAX_MESSAGES; i++)
		_queue[i] = 0;
}

#endif
