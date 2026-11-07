#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

int strpcbInit(void);
ssize_t strpcbRead(int fd, void *buf, size_t count);
ssize_t strpcbWrite(int fd, const void *buf, size_t count);
int strpcbIoctl(int fd, unsigned long request, void *data);
int strpcbClose(int fd);

/* [2][3] centre (10-bit, low first), [4] spring, [6] viscosity, [8] reflect (signed) */
typedef void (*StrpcbFrameSink)(const uint8_t frame[10]);
void strpcb_set_frame_sink(StrpcbFrameSink sink);

typedef uint16_t (*StrpcbPositionSource)(void);
void strpcb_set_position_source(StrpcbPositionSource source);

void strpcb_set_instance(void *const *instance);

/* The self check is a ~1 frame pulse: answer it from the request call. */
void strpcb_report_motor_power(bool running);
void strpcb_report_self_check(void);
