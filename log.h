#pragma once
#include <stdbool.h>
void log_set_echo(bool new_value);
void log_write(FILE *f, const char *fmt, ...);
