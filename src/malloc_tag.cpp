#include "malloc_tag.h"

extern void *__libc_malloc(size_t size);
int malloc_hook_active = 1;

void *
my_malloc_hook(size_t size, void *caller)
{
    void *result;

    // deactivate hooks for logging
    malloc_hook_active = 0;

    result = malloc(size);

    // do logging
    printf("ciao %zu", size);

    // reactivate hooks
    malloc_hook_active = 1;

    return result;
}

void *
malloc(size_t size)
{
    void *caller = __builtin_return_address(0);
    if (malloc_hook_active)
        return my_malloc_hook(size, caller);
    return __libc_malloc(size);
}