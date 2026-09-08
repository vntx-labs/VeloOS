#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <velo/syscall.h>

extern int main(int argc, char **argv);

// ====================================================
// STANDARD I/O STREAMS & RAM BUFFER
// ====================================================
static char _stdout_buf = 0;
static char _stderr_buf = 0;
static char _stdin_buf = 0;
void *stdout = (void*)&_stdout_buf;
void *stderr = (void*)&_stderr_buf;
void *stdin  = (void*)&_stdin_buf;

static char g_stdout_ram_buf[32768];
static size_t g_stdout_ram_pos = 0;

static void flush_stdout_to_disk(void) {
    if (g_stdout_ram_pos == 0) return;
    int ok = velo_write_file("/VeloOS/System32/STDOUT.DAT", g_stdout_ram_buf, (UINT32)g_stdout_ram_pos);
    if (!ok) {
        velo_write_file("/STDOUT.DAT", g_stdout_ram_buf, (UINT32)g_stdout_ram_pos);
    }
}

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
// BEFEHLSZEILEN-ARGUMENTE (ARGC, ARGV)
// ====================================================
static char g_cmdline_buf[1024];
static char *g_cmd_argv[64];

static int prepare_cmdline_args(void) {
    int bytes = velo_read_file("/VeloOS/System32/CMDLINE.DAT", g_cmdline_buf, sizeof(g_cmdline_buf) - 1);
    if (bytes <= 0) {
        bytes = velo_read_file("/CMDLINE.DAT", g_cmdline_buf, sizeof(g_cmdline_buf) - 1);
    }

    if (bytes <= 0) {
        g_cmd_argv[0] = "app";
        g_cmd_argv[1] = NULL;
        return 1;
    }
    g_cmdline_buf[bytes] = '\0';

    int argc = 0;
    char *p = g_cmdline_buf;
    char in_quote = 0;

    while (*p && argc < 63) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        g_cmd_argv[argc++] = p;
        while (*p) {
            if (!in_quote && (*p == '\'' || *p == '"')) {
                in_quote = *p;
            } else if (in_quote && *p == in_quote) {
                in_quote = 0;
            } else if (!in_quote && isspace((unsigned char)*p)) {
                break;
            }
            p++;
        }
        if (*p) *p++ = '\0';
    }
    g_cmd_argv[argc] = NULL;
    return (argc > 0) ? argc : 1;
}

// ====================================================
// RING 3 USERLAND ENTRY POINT (CPL=3)
// ====================================================
void __libc_entry(void) {
    g_stdout_ram_pos = 0;
    init_cpp_ctors();
    int argc = prepare_cmdline_args();
    int exit_code = main(argc, g_cmd_argv);
    exit(exit_code);
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "movw $0x1B, %ax\n\t"     // USER_DS (0x18 | 3) in DS & ES laden
        "movw %ax, %ds\n\t"
        "movw %ax, %es\n\t"
        "xorq %rbp, %rbp\n\t"
        "andq $-16, %rsp\n\t"     // 16-Byte Stack Alignment fuer x86-64 ABI
        "call __libc_entry\n\t"   // Initialisieren und main(argc, argv) aufrufen
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

    if (key == (char)0x84) {
        if (pos > 0) *cursor_pos = pos - 1;
        return 1;
    } else if (key == (char)0x85) {
        if (pos < len) *cursor_pos = pos + 1;
        return 1;
    } else if (key == (char)0x86) {
        *cursor_pos = 0;
        return 1;
    } else if (key == (char)0x87) {
        *cursor_pos = len;
        return 1;
    } else if (key == (char)0x88 || key == 0x7F) {
        if (pos < len) {
            for (int i = pos; i < len; i++) text[i] = text[i + 1];
            return 1;
        }
    } else if (key == '\b') {
        if (pos > 0) {
            for (int i = pos - 1; i < len; i++) text[i] = text[i + 1];
            *cursor_pos = pos - 1;
            return 1;
        }
    } else if ((unsigned char)key >= 32 && len < max_len - 1) {
        for (int i = len; i >= pos; i--) text[i + 1] = text[i];
        text[pos] = key;
        *cursor_pos = pos + 1;
        return 1;
    }
    return 0;
}

int velo_poll_event(int win, velo_event_t *ev) {
    if (!ev) return -1;
    memset(ev, 0, sizeof(velo_event_t));
    return (int)velo_syscall(SYS_GET_EVENT, (UINT64)win, (UINT64)ev, 0, 0);
}

// ====================================================
// CTYPE ERGÄNZUNGEN
// ====================================================
int isprint(int c) {
    return (c >= 32 && c <= 126);
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

char *strncat(char *dest, const char *src, size_t n) {
    size_t dlen = strlen(dest);
    size_t i = 0;
    while (i < n && src && src[i]) { dest[dlen + i] = src[i]; i++; }
    dest[dlen + i] = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2 || n == 0) return 0;
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || !s1[i]) return (int)(unsigned char)s1[i] - (int)(unsigned char)s2[i];
    }
    return 0;
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
    if (!s1 || !s2 || n == 0) return 0;
    for (size_t i = 0; i < n; i++) {
        if (!s1[i] || !s2[i]) return (unsigned char)tolower((unsigned char)s1[i]) - (unsigned char)tolower((unsigned char)s2[i]);
        char c1 = tolower((unsigned char)s1[i]);
        char c2 = tolower((unsigned char)s2[i]);
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
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

char *strtok_r(char *str, const char *delim, char **saveptr) {
    char *s = str ? str : *saveptr;
    if (!s) return NULL;

    while (*s && strchr(delim, *s)) s++;
    if (!*s) {
        *saveptr = NULL;
        return NULL;
    }

    char *token = s;
    while (*s && !strchr(delim, *s)) s++;
    if (*s) {
        *s = '\0';
        *saveptr = s + 1;
    } else {
        *saveptr = NULL;
    }
    return token;
}

static char *g_strtok_ctx = NULL;
char *strtok(char *str, const char *delim) {
    return strtok_r(str, delim, &g_strtok_ctx);
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
// HEAP ALLOKATOR
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
    if (!block) return NULL;

    block->size = chunk_size - sizeof(block_header_t);
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
    flush_stdout_to_disk();
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

long atol(const char *s) {
    long res = 0; int sign = 1;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (isdigit((unsigned char)*s)) { res = res * 10 + (*s - '0'); s++; }
    return res * sign;
}

int abs(int j) { return (j < 0) ? -j : j; }

static void u64_to_str(UINT64 val, char *buf, int base, int is_signed) {
    char tmp[65];
    int i = 0;
    int sign = 0;

    if (is_signed && (INT64)val < 0) {
        sign = 1;
        val = (UINT64)(-(INT64)val);
    }

    if (val == 0) tmp[i++] = '0';
    else {
        while (val > 0) {
            int rem = (int)(val % (UINT64)base);
            tmp[i++] = (rem < 10) ? ('0' + rem) : ('a' + rem - 10);
            val /= (UINT64)base;
        }
    }

    if (sign) tmp[i++] = '-';

    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

char *itoa(int value, char *str, int base) {
    u64_to_str((UINT64)(INT64)value, str, base, 1);
    return str;
}

static unsigned long int g_next_rand = 1;
int rand(void) {
    g_next_rand = g_next_rand * 1103515245 + 12345;
    return (unsigned int)(g_next_rand / 65536) % 32768;
}
void srand(unsigned int seed) { g_next_rand = seed; }

// ====================================================
// PRINTF & SCHNELLER RAM-PUFFER (KEIN LAG BEIM SCHREIBEN)
// ====================================================
static void append_stdout(const char *buf, size_t len) {
    if (!buf || len == 0) return;
    if (g_stdout_ram_pos + len < sizeof(g_stdout_ram_buf) - 1) {
        memcpy(g_stdout_ram_buf + g_stdout_ram_pos, buf, len);
        g_stdout_ram_pos += len;
        g_stdout_ram_buf[g_stdout_ram_pos] = '\0';
    }
}

int puts(const char *s) {
    if (!s) return 0;
    append_stdout(s, strlen(s));
    append_stdout("\n", 1);
    return 1;
}

int putchar(int c) {
    char ch = (char)c;
    append_stdout(&ch, 1);
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

        int is_long = 0;
        int width = 0;
        char pad = ' ';

        while (*format == '-' || *format == '+' || *format == ' ' || *format == '0') {
            if (*format == '0') pad = '0';
            format++;
        }

        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }

        while (*format == 'l' || *format == 'h' || *format == 'z') {
            if (*format == 'l') is_long++;
            format++;
        }

        char num_buf[64];
        num_buf[0] = '\0';

        if (*format == 'd' || *format == 'i') {
            if (is_long >= 2) {
                long long val = va_arg(ap, long long);
                u64_to_str((UINT64)val, num_buf, 10, 1);
            } else if (is_long == 1) {
                long val = va_arg(ap, long);
                u64_to_str((UINT64)val, num_buf, 10, 1);
            } else {
                int val = va_arg(ap, int);
                u64_to_str((UINT64)(INT64)val, num_buf, 10, 1);
            }
            int nlen = (int)strlen(num_buf);
            while (width > nlen && pos < size - 1) { str[pos++] = pad; width--; }
            for (size_t k = 0; num_buf[k] && pos < size - 1; k++) str[pos++] = num_buf[k];
        } else if (*format == 'u') {
            if (is_long >= 2) {
                unsigned long long val = va_arg(ap, unsigned long long);
                u64_to_str(val, num_buf, 10, 0);
            } else if (is_long == 1) {
                unsigned long val = va_arg(ap, unsigned long);
                u64_to_str((UINT64)val, num_buf, 10, 0);
            } else {
                unsigned int val = va_arg(ap, unsigned int);
                u64_to_str((UINT64)val, num_buf, 10, 0);
            }
            int nlen = (int)strlen(num_buf);
            while (width > nlen && pos < size - 1) { str[pos++] = pad; width--; }
            for (size_t k = 0; num_buf[k] && pos < size - 1; k++) str[pos++] = num_buf[k];
        } else if (*format == 'x' || *format == 'X' || *format == 'p') {
            UINT64 val;
            if (*format == 'p' || is_long >= 1) val = va_arg(ap, UINT64);
            else val = (UINT64)va_arg(ap, unsigned int);

            u64_to_str(val, num_buf, 16, 0);
            int nlen = (int)strlen(num_buf);
            while (width > nlen && pos < size - 1) { str[pos++] = pad; width--; }
            for (size_t k = 0; num_buf[k] && pos < size - 1; k++) str[pos++] = num_buf[k];
        } else if (*format == 's') {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int slen = (int)strlen(s);
            while (width > slen && pos < size - 1) { str[pos++] = ' '; width--; }
            while (*s && pos < size - 1) str[pos++] = *s++;
        } else if (*format == 'c') {
            char c = (char)va_arg(ap, int);
            str[pos++] = c;
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
    int res = vsnprintf(str, 2048, format, ap);
    va_end(ap);
    return res;
}

int printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    char buf[4096];
    int res = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    if (res > 0) {
        append_stdout(buf, (size_t)res);
    }
    return res;
}

int vprintf(const char *format, va_list ap) {
    char buf[4096];
    int res = vsnprintf(buf, sizeof(buf), format, ap);
    if (res > 0) {
        append_stdout(buf, (size_t)res);
    }
    return res;
}

int fprintf(void *stream, const char *format, ...) {
    (void)stream;
    va_list ap;
    va_start(ap, format);
    char buf[4096];
    int res = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    if (res > 0) {
        append_stdout(buf, (size_t)res);
    }
    return res;
}

int vfprintf(void *stream, const char *format, va_list ap) {
    (void)stream;
    char buf[4096];
    int res = vsnprintf(buf, sizeof(buf), format, ap);
    if (res > 0) {
        append_stdout(buf, (size_t)res);
    }
    return res;
}

int fputs(const char *s, void *stream) {
    (void)stream;
    return puts(s);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, void *stream) {
    (void)stream;
    size_t total = size * nmemb;
    append_stdout((const char*)ptr, total);
    return total;
}

int fflush(void *stream) {
    (void)stream;
    flush_stdout_to_disk();
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

int _stricmp(const char *s1, const char *s2) { return strcasecmp(s1, s2); }
int _strnicmp(const char *s1, const char *s2, size_t n) { return strncasecmp(s1, s2, n); }