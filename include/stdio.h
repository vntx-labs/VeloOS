#ifndef _STDIO_H
#define _STDIO_H

#include <stdlib.h>
#include <stdarg.h>

int puts(const char *s);
int putchar(int c);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

#endif