#pragma once
#include <stdbool.h>
void log_set_echo(bool new_value);
void log_write(const char *fmt, ...);
