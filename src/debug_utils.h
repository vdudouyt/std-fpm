#pragma once
#include <stdio.h>
#include <ctype.h>

void hexdump(const char *buf, size_t size);
void escape(char *out, const char *in, size_t input_length);
