#ifndef _VELO_STDIO_H
#define _VELO_STDIO_H

#include <stdlib.h>
#include <stdarg.h>

#define EOF (-1)

typedef struct {
    int fd;
} FILE;

typedef long fpos_t;

int puts(const char *s);
int putchar(int c);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

#endif