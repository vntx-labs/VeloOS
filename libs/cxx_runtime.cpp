#include <stddef.h>
#include <stdlib.h>

extern "C" {
    void* __dso_handle = (void*)0;

    int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
    void __cxa_finalize(void*) {}
    void __cxa_pure_virtual() { while (1) {} }
    void __cxa_deleted_virtual() { while (1) {} }
}

void* operator new(size_t size) {
    return malloc(size ? size : 1);
}

void* operator new[](size_t size) {
    return malloc(size ? size : 1);
}

void operator delete(void* ptr) noexcept {
    free(ptr);
}

void operator delete[](void* ptr) noexcept {
    free(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    free(ptr);
}