#pragma once

typedef long time_t;
struct timespec { time_t tv_sec; long tv_nsec; };
int nanosleep(const struct timespec *request, struct timespec *remaining);
