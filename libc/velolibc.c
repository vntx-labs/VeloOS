#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <velo/syscall.h>

// ==========================================
// USERLAND CRT STARTUP (_start -> main -> retq)
// ==========================================
extern int main(int argc, char **argv);

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "pushq %rbx\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "pushq %rbp\n\t"
        "movq %rsp, %rbp\n\t"
        "andq $-16, %rsp\n\t"
        "subq $8, %rsp\n\t"
        "xorq %rdi, %rdi\n\t"
        "xorq %rsi, %rsi\n\t"
        "call main\n\t"
        "movq %rbp, %rsp\n\t"
        "popq %rbp\n\t"
        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %rbx\n\t"
        "retq\n\t"
    );
}

// ==========================================
// STRING.H IMPLEMENTIERUNG
// ==========================================
size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char *strcpy(char *dest, const char *src) {
    size_t i = 0;
    while (src[i]) { dest[i] = src[i]; i++; }
    dest[i] = '\0';
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    while (i < n && src[i]) { dest[i] = src[i]; i++; }
    while (i < n) { dest[i] = '\0'; i++; }
    return dest;
}

char *strcat(char *dest, const char *src) {
    size_t dlen = strlen(dest);
    size_t i = 0;
    while (src[i]) { dest[dlen + i] = src[i]; i++; }
    dest[dlen + i] = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || !s1[i]) return (int)(unsigned char)s1[i] - (int)(unsigned char)s2[i];
    }
    return 0;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    do {
        if (*s == (char)c) last = s;
    } while (*s++);
    return (char*)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;
    for (size_t i = 0; haystack[i]; i++) {
        size_t k = 0;
        while (needle[k] && haystack[i + k] == needle[k]) k++;
        if (!needle[k]) return (char*)&haystack[i];
    }
    return NULL;
}

void *memcpy(void *dest, const void *src, size_t n) {
    char *d = (char*)dest;
    const char *s = (const char*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    char *d = (char*)dest;
    const char *s = (const char*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

void *memset(void *s, int c, size_t n) {
    char *p = (char*)s;
    for (size_t i = 0; i < n; i++) p[i] = (char)c;
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char*)s1;
    const unsigned char *p2 = (const unsigned char*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
}

// ==========================================
// STDLIB.H & DYNAMISCHER HEAP-ALLOCATOR
// ==========================================
typedef struct block_header {
    size_t size;
    int is_free;
    struct block_header *next;
} block_header_t;

static block_header_t *g_heap_head = NULL;

void *malloc(size_t size) {
    if (size == 0) return NULL;

    size_t aligned_size = (size + 7) & ~7;
    block_header_t *curr = g_heap_head;
    block_header_t *prev = NULL;

    while (curr) {
        if (curr->is_free && curr->size >= aligned_size) {
            curr->is_free = 0;
            return (void*)(curr + 1);
        }
        prev = curr;
        curr = curr->next;
    }

    size_t total_alloc = sizeof(block_header_t) + aligned_size;
    if (total_alloc < 4096) total_alloc = 4096;

    block_header_t *block = (block_header_t*)(void*)velo_syscall(SYS_HEAP_ALLOC, total_alloc, 0, 0, 0);
    if (!block) return NULL;

    block->size = total_alloc - sizeof(block_header_t);
    block->is_free = 0;
    block->next = NULL;

    if (prev) prev->next = block;
    else g_heap_head = block;

    return (void*)(block + 1);
}

void free(void *ptr) {
    if (!ptr) return;
    block_header_t *block = ((block_header_t*)ptr) - 1;
    block->is_free = 1;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }

    block_header_t *block = ((block_header_t*)ptr) - 1;
    if (block->size >= size) return ptr;

    void *new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        free(ptr);
    }
    return new_ptr;
}

void exit(int status) {
    velo_syscall(SYS_EXIT, (UINT64)status, 0, 0, 0);
    while (1) {}
}

int atoi(const char *s) {
    int res = 0, sign = 1;
    while (isspace(*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (isdigit(*s)) { res = res * 10 + (*s - '0'); s++; }
    return res * sign;
}

int abs(int j) { return (j < 0) ? -j : j; }

char *itoa(int value, char *str, int base) {
    if (base < 2 || base > 36) { *str = '\0'; return str; }
    char *ptr = str, *ptr1 = str, tmp_char;
    int tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[abs(tmp_value - value * base)];
    } while (value);

    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';

    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    return str;
}

static unsigned long int g_next_rand = 1;
int rand(void) {
    g_next_rand = g_next_rand * 1103515245 + 12345;
    return (unsigned int)(g_next_rand / 65536) % 32768;
}
void srand(unsigned int seed) { g_next_rand = seed; }

// ==========================================
// STDIO.H FORMATIERTER TEXT-OUTPUT
// ==========================================
int puts(const char *s) {
    while (*s) { putchar(*s++); }
    putchar('\n');
    return 1;
}

int putchar(int c) {
    char buf[2] = {(char)c, '\0'};
    (void)buf;
    return c;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (!str || size == 0) return 0;
    size_t pos = 0;

    while (*format && pos < size - 1) {
        if (*format != '%') {
            str[pos++] = *format++;
            continue;
        }
        format++;

        while (*format == '-' || *format == '+' || *format == ' ' || *format == '0' ||
               (*format >= '0' && *format <= '9')) {
            format++;
        }

        if (*format == 'd' || *format == 'i') {
            int val = va_arg(ap, int);
            char num_buf[32];
            itoa(val, num_buf, 10);
            for (size_t k = 0; num_buf[k] && pos < size - 1; k++) str[pos++] = num_buf[k];
        } else if (*format == 's') {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            while (*s && pos < size - 1) str[pos++] = *s++;
        } else if (*format == 'c') {
            char c = (char)va_arg(ap, int);
            str[pos++] = c;
        } else if (*format == 'x' || *format == 'X') {
            int val = va_arg(ap, int);
            char hex_buf[32];
            itoa(val, hex_buf, 16);
            for (size_t k = 0; hex_buf[k] && pos < size - 1; k++) str[pos++] = hex_buf[k];
        } else if (*format == 'u') {
            unsigned int val = va_arg(ap, unsigned int);
            char num_buf[32];
            itoa((int)val, num_buf, 10);
            for (size_t k = 0; num_buf[k] && pos < size - 1; k++) str[pos++] = num_buf[k];
        } else if (*format == '%') {
            str[pos++] = '%';
        }

        if (*format) format++;
    }
    str[pos] = '\0';
    return (int)pos;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vsnprintf(str, size, format, ap);
    va_end(ap);
    return res;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vsnprintf(str, 1024, format, ap);
    va_end(ap);
    return res;
}