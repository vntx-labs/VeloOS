#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <velo/syscall.h>

extern int main(int argc, char **argv);

// ====================================================
// STANDARD I/O STREAMS
// ====================================================
static char _stdout_buf = 0;
static char _stderr_buf = 0;
static char _stdin_buf = 0;
void *stdout = (void*)&_stdout_buf;
void *stderr = (void*)&_stderr_buf;
void *stdin = (void*)&_stdin_buf;

// ELF Konstruktor-Array fuer C++ Global-/Static-Objects (LiteHTML)
extern void (*__init_array_start[])(void) __attribute__((weak));
extern void (*__init_array_end[])(void) __attribute__((weak));

__attribute__((used)) void init_cpp_ctors(void) {
    if (__init_array_start && __init_array_end) {
        size_t count = (size_t)(__init_array_end - __init_array_start);
        if (count > 0 && count < 4096) {
            for (size_t i = 0; i < count; i++) {
                if (__init_array_start[i]) {
                    __init_array_start[i]();
                }
            }
        }
    }
}

// ====================================================
// RING 3 USERLAND ENTRY POINT (CPL=3)
// ====================================================
__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "movw $0x1B, %ax\n\t"     // USER_DS (0x18 | 3) in DS & ES laden
        "movw %ax, %ds\n\t"
        "movw %ax, %es\n\t"
        "xorq %rbp, %rbp\n\t"
        "andq $-16, %rsp\n\t"     // 16-Byte Stack Alignment fuer x86-64 ABI
        "call init_cpp_ctors\n\t" // C++ Globale Konstruktoren initialisieren
        "xorq %rdi, %rdi\n\t"     // argc = 0
        "xorq %rsi, %rsi\n\t"     // argv = NULL
        "call main\n\t"
        "movq %rax, %rdi\n\t"
        "xorq %rdi, %rdi\n\t"
        "movq $0, %rax\n\t"       // SYS_EXIT = 0
        "int $0x80\n\t"
        "1: pause; jmp 1b\n\t"
    );
}

// ====================================================
// UNIVERSELLE TEXTBOX- & CURSOR-STEUERUNG
// ====================================================
int velo_ui_textbox_handle_key(char *text, int max_len, int *cursor_pos, char key) {
    if (!text || !cursor_pos || max_len <= 1) return 0;
    int len = (int)strlen(text);
    int pos = *cursor_pos;
    if (pos > len) pos = len;
    if (pos < 0) pos = 0;

    // Pfeil Links (0x84)
    if (key == (char)0x84) {
        if (pos > 0) *cursor_pos = pos - 1;
        return 1;
    }
    // Pfeil Rechts (0x85)
    else if (key == (char)0x85) {
        if (pos < len) *cursor_pos = pos + 1;
        return 1;
    }
    // Pos1 / Home (0x86)
    else if (key == (char)0x86) {
        *cursor_pos = 0;
        return 1;
    }
    // Ende / End (0x87)
    else if (key == (char)0x87) {
        *cursor_pos = len;
        return 1;
    }
    // Entf / Delete (0x88 oder 0x7F) -> Zeichen rechts vom Cursor löschen
    else if (key == (char)0x88 || key == 0x7F) {
        if (pos < len) {
            for (int i = pos; i < len; i++) text[i] = text[i + 1];
            return 1;
        }
    }
    // Backspace (\b) -> Zeichen links vom Cursor löschen
    else if (key == '\b') {
        if (pos > 0) {
            for (int i = pos - 1; i < len; i++) text[i] = text[i + 1];
            *cursor_pos = pos - 1;
            return 1;
        }
    }
    // Normales druckbares Zeichen einfügen
    else if ((unsigned char)key >= 32 && len < max_len - 1) {
        for (int i = len; i >= pos; i--) text[i + 1] = text[i];
        text[pos] = key;
        *cursor_pos = pos + 1;
        return 1;
    }
    return 0;
}

// ====================================================
// STRING- & SPEICHER-FUNKTIONEN
// ====================================================
size_t strlen(const char *s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

char *strcpy(char *dest, const char *src) {
    size_t i = 0;
    while (src && src[i]) { dest[i] = src[i]; i++; }
    dest[i] = '\0';
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    while (i < n && src && src[i]) { dest[i] = src[i]; i++; }
    while (i < n) { dest[i] = '\0'; i++; }
    return dest;
}

char *strcat(char *dest, const char *src) {
    size_t dlen = strlen(dest);
    size_t i = 0;
    while (src && src[i]) { dest[dlen + i] = src[i]; i++; }
    dest[dlen + i] = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2) return 0;
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || !s1[i]) return (int)(unsigned char)s1[i] - (int)(unsigned char)s2[i];
    }
    return 0;
}

char *strchr(const char *s, int c) {
    if (!s) return NULL;
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : NULL;
}

char *strrchr(const char *s, int c) {
    if (!s) return NULL;
    const char *last = NULL;
    do {
        if (*s == (char)c) last = s;
    } while (*s++);
    return (char*)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
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

// ====================================================
// HEAP ALLOKATOR MIT BLOCK-SPLITTING
// ====================================================
typedef struct block_header {
    size_t size;
    int is_free;
    struct block_header *next;
} block_header_t;

static block_header_t *g_heap_head = NULL;

void *malloc(size_t size) {
    if (size == 0) return NULL;

    size_t aligned_size = (size + 15) & ~15;
    block_header_t *curr = g_heap_head;
    block_header_t *prev = NULL;

    while (curr) {
        if (curr->is_free && curr->size >= aligned_size) {
            if (curr->size >= aligned_size + sizeof(block_header_t) + 32) {
                block_header_t *next_block = (block_header_t*)((char*)(curr + 1) + aligned_size);
                next_block->size = curr->size - aligned_size - sizeof(block_header_t);
                next_block->is_free = 1;
                next_block->next = curr->next;
                curr->size = aligned_size;
                curr->next = next_block;
            }
            curr->is_free = 0;
            return (void*)(curr + 1);
        }
        prev = curr;
        curr = curr->next;
    }

    size_t chunk_size = aligned_size + sizeof(block_header_t);
    if (chunk_size < 65536) chunk_size = 65536;

    block_header_t *block = (block_header_t*)(void*)velo_syscall(SYS_HEAP_ALLOC, chunk_size, 0, 0, 0);
    if (!block) {
        chunk_size = aligned_size + sizeof(block_header_t);
        block = (block_header_t*)(void*)velo_syscall(SYS_HEAP_ALLOC, chunk_size, 0, 0, 0);
        if (!block) return NULL;
    }

    block->size = chunk_size - sizeof(block_header_t);
    block->is_free = 0;
    block->next = NULL;

    if (prev) prev->next = block;
    else g_heap_head = block;

    if (block->size >= aligned_size + sizeof(block_header_t) + 32) {
        block_header_t *rem = (block_header_t*)((char*)(block + 1) + aligned_size);
        rem->size = block->size - aligned_size - sizeof(block_header_t);
        rem->is_free = 1;
        rem->next = NULL;
        block->size = aligned_size;
        block->next = rem;
    }

    return (void*)(block + 1);
}

void free(void *ptr) {
    if (!ptr) return;
    block_header_t *block = ((block_header_t*)ptr) - 1;
    block->is_free = 1;

    block_header_t *curr = g_heap_head;
    while (curr && curr->next) {
        if (curr->is_free && curr->next->is_free &&
            (char*)(curr + 1) + curr->size == (char*)curr->next) {
            curr->size += sizeof(block_header_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
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
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (isdigit((unsigned char)*s)) { res = res * 10 + (*s - '0'); s++; }
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

// ====================================================
// PRINTF & FORMATTING
// ====================================================
int puts(const char *s) { (void)s; return 1; }
int putchar(int c) { return c; }

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

int printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    char buf[1024];
    int res = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    return res;
}

int vprintf(const char *format, va_list ap) {
    char buf[1024];
    return vsnprintf(buf, sizeof(buf), format, ap);
}

int fprintf(void *stream, const char *format, ...) {
    (void)stream;
    va_list ap;
    va_start(ap, format);
    char buf[1024];
    int res = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    return res;
}

int vfprintf(void *stream, const char *format, va_list ap) {
    (void)stream;
    char buf[1024];
    return vsnprintf(buf, sizeof(buf), format, ap);
}

int fputs(const char *s, void *stream) {
    (void)s; (void)stream;
    return 1;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, void *stream) {
    (void)ptr; (void)stream;
    return size * nmemb;
}

int fflush(void *stream) {
    (void)stream;
    return 0;
}

void abort(void) {
    exit(1);
}

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
    (void)assertion; (void)file; (void)line; (void)function;
    exit(1);
}

void __stack_chk_fail(void) {
    exit(1);
}

int strcasecmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && *s2) {
        char c1 = tolower((unsigned char)*s1);
        char c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        s1++; s2++;
    }
    return (unsigned char)tolower((unsigned char)*s1) - (unsigned char)tolower((unsigned char)*s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2) return 0;
    for (size_t i = 0; i < n; i++) {
        if (!s1[i] || !s2[i]) return (unsigned char)tolower((unsigned char)s1[i]) - (unsigned char)tolower((unsigned char)s2[i]);
        char c1 = tolower((unsigned char)s1[i]);
        char c2 = tolower((unsigned char)s2[i]);
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
    }
    return 0;
}

int _stricmp(const char *s1, const char *s2) { return strcasecmp(s1, s2); }
int _strnicmp(const char *s1, const char *s2, size_t n) { return strncasecmp(s1, s2, n); }