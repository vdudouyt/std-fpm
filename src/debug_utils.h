#pragma once
#include <stdio.h>
#include <ctype.h>

void hexdump(const unsigned char *buf, size_t size);
void escape(unsigned char *out, const unsigned char *in, size_t input_length);
