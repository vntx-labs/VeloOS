#ifndef _STDLIB_H
#define _STDLIB_H

#include <string.h>

#define NULL ((void*)0)

void  *malloc(size_t size);
void   free(void *ptr);
void  *calloc(size_t nmemb, size_t size);
void  *realloc(void *ptr, size_t size);

void   exit(int status);
int    atoi(const char *nptr);
int    abs(int j);
char  *itoa(int value, char *str, int base);

int    rand(void);
void   srand(unsigned int seed);

#endif