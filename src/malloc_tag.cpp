/*
 * malloc_tag.cpp
 * Malloc hooks that implement a low-overhead memory profiler
 *
 * Inspired by:
 *  - Pixar TfMallocTag tool, see https://openusd.org/dev/api/page_tf__malloc_tag.html
 *
 * Author: fmontorsi
 * Created: Aug 2023
 * License: Apache license
 *
 */

//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------

#include "malloc_tag.h"
#include "private/fmpool.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "private/malloc_tree.h"
#include "private/malloc_tree_node.h"
#include "private/malloc_tree_registry.h"

//------------------------------------------------------------------------------
// Macros
//------------------------------------------------------------------------------

#define UNLIKELY(x) __builtin_expect((x), 0)

//------------------------------------------------------------------------------
// External functions
//------------------------------------------------------------------------------

extern "C" {
// these __libc_* functions are the actual glibc implementation:
void* __libc_malloc(size_t size);
void __libc_free(void*);
void* __libc_realloc(void* ptr, size_t newsize);
void* __libc_calloc(size_t count, size_t eltsize);
void* __libc_memalign(size_t alignment, size_t size);
void* __libc_valloc(size_t __size);
void* __libc_pvalloc(size_t __size);
};

//------------------------------------------------------------------------------
// Globals
//------------------------------------------------------------------------------

// main enable/disable flag:
thread_local bool g_perthread_malloc_hook_active = true;

// the main global per-thread malloc tree:
thread_local MallocTree* g_perthread_tree = nullptr;

// the global registry for all threads; this is NOT thread-specific, but is thread-safe
MallocTreeRegistry g_registry;

// this accounts for ALL mallocs done by ALL threads before MallocTagEngine::init()
std::atomic<size_t> g_bytes_allocated_before_init;

//------------------------------------------------------------------------------
// Utils
//------------------------------------------------------------------------------

class HookDisabler {
public:
    HookDisabler()
    {
        // note that if HookDisabler instances are nested, it might happen that currently
        // g_perthread_malloc_hook_active == false; if that's the case, the dtor of the
        // HookDisabler shall NOT re-enable hooks.
        // E.g. consider:
        /*
    {
        HookDisabler firstDisabler;

        {
            HookDisabler nestedDisabler;
        } // dtor of nestedDisabler invoked

        // ...some function whose memory usage must NOT be accounted for...

    } // dtor of firstDisabler invoked
*/
        m_prev_state = g_perthread_malloc_hook_active;
        g_perthread_malloc_hook_active = false;
    }
    ~HookDisabler() { g_perthread_malloc_hook_active = m_prev_state; }

private:
    bool m_prev_state = false;
};

//------------------------------------------------------------------------------
// MallocTagScope
//------------------------------------------------------------------------------

MallocTagScope::MallocTagScope(const char* tag_name)
{
    // advance the per-thread cursor inside the malloc tree by 1 more level, adding the "tag_name" level
    // VERY IMPORTANT: all code running in this function must be malloc-free
    assert(g_perthread_tree
        && g_perthread_tree->is_ready()); // it's a logical mistake to use MallocTagScope before MallocTagEngine::init()
    g_perthread_tree->push_new_node(tag_name);
}

MallocTagScope::MallocTagScope(const char* class_name, const char* function_name)
{
    // advance the per-thread cursor inside the malloc tree by 1 more level, adding the "tag_name" level
    // VERY IMPORTANT: all code running in this function must be malloc-free
    assert(g_perthread_tree
        && g_perthread_tree->is_ready()); // it's a logical mistake to use MallocTagScope before MallocTagEngine::init()

    char scopeName[MTAG_MAX_SCOPENAME_LEN] = { '\0' };
    strncpy(scopeName, class_name, MTAG_MAX_SCOPENAME_LEN - 1); // try to abbreviate class_name if it's too long!
    strncat(scopeName, "::", MTAG_MAX_SCOPENAME_LEN - strlen(scopeName) - 1);
    strncat(scopeName, function_name, MTAG_MAX_SCOPENAME_LEN - strlen(scopeName) - 1);

    g_perthread_tree->push_new_node(scopeName);
}

MallocTagScope::~MallocTagScope()
{
    // pop by 1 level the current per-thread cursor
    // VERY IMPORTANT: all code running in this function must be malloc-free
    assert(g_perthread_tree
        && g_perthread_tree->is_ready()); // it's a logical mistake to use MallocTagScope before MallocTagEngine::init()
    g_perthread_tree->pop_last_node();
}

//------------------------------------------------------------------------------
// MallocTagEngine
//------------------------------------------------------------------------------

bool MallocTagEngine::init(size_t max_tree_nodes, size_t max_tree_levels)
{
    if (UNLIKELY(g_perthread_tree && g_perthread_tree->is_ready()))
        return true; // invoking twice? not a failure but suspicious

    // init the main-thread tree:
    // this will "unblock" the creation of malloc trees for all other threads, see malloc() logic
    {
        HookDisabler doNotAccountSelfMemoryUsage;
        g_perthread_tree = g_registry.register_main_tree(max_tree_nodes, max_tree_levels);
    }

    return g_perthread_tree != nullptr;
}

std::string MallocTagEngine::collect_stats(MallocTagOutputFormat_e format, const std::string& output_options)
{
    HookDisabler doNotAccountCollectStatMemoryUsage;

    // reserve enough space inside the output string:
    std::string stats_str;
    stats_str.reserve(4096);

    g_registry.collect_stats(stats_str, format, output_options);

    return stats_str;
}

bool __internal_write_stats_on_disk(
    MallocTagOutputFormat_e format, const std::string& fullpath, const std::string& output_options)
{
    bool bwritten = false;

    std::string fpath = fullpath;
    if (fpath.empty()) {
        // try to use an env var:
        switch (format) {
        case MTAG_OUTPUT_FORMAT_JSON:
            if (getenv(MTAG_STATS_OUTPUT_JSON_ENV))
                fpath = std::string(getenv(MTAG_STATS_OUTPUT_JSON_ENV));
            break;

        case MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT:
            if (getenv(MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV))
                fpath = std::string(getenv(MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV));
            break;

        case MTAG_OUTPUT_FORMAT_ALL:
            assert(0); // caller must deal with MTAG_OUTPUT_FORMAT_ALL
            break;
        }
    }

    std::ofstream stats_file(fpath);
    if (stats_file.is_open()) {
        stats_file << MallocTagEngine::collect_stats(format, output_options) << std::endl;
        bwritten = true;
        stats_file.close();
    }

    return bwritten;
}

bool MallocTagEngine::write_stats_on_disk(
    MallocTagOutputFormat_e format, const std::string& fullpath, const std::string& output_options)
{
    HookDisabler doNotAccountCollectStatMemoryUsage;

    bool bwritten = false;
    switch (format) {
    case MTAG_OUTPUT_FORMAT_ALL:
        // if asked to write stats into multiple output format,
        // disable the hooks just once and re-enable the hooks only when done for all output formats;
        // this is important to maintain coherency between the data we write in the different output files:
        // if HookDisabler goes out of scope between the first & second output generation steps, we risk
        // that the second step contains more collected data compared to the first one
        bwritten = __internal_write_stats_on_disk(MTAG_OUTPUT_FORMAT_JSON, fullpath, output_options);
        bwritten &= __internal_write_stats_on_disk(MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT, fullpath, output_options);
        break;

    default:
        bwritten = __internal_write_stats_on_disk(format, fullpath, output_options);
    }

    return bwritten;
}

int parseLine(char* line)
{
    // This assumes that a digit will be found and the line ends in " Kb".
    int i = strlen(line);
    const char* p = line;
    while (*p < '0' || *p > '9')
        p++;
    line[i - 3] = '\0';
    return atoi(p); // it will be zero on error
}

size_t MallocTagEngine::get_linux_vmsize_in_bytes()
{
    char line[128];

    // IMPORTANT: remember that Linux will not account memory usage separately on a thread-basis.
    //            so even if this software is multi-threaded, all files under
    //    /proc/<PID>/task/<TID>/status
    // will report identical VmSize, VmRSS, etc etc
    // So this function can and will always return only the GRAND TOTAL of virtual memory taken by the whole process
    FILE* file = fopen("/proc/self/status", "r");
    if (!file)
        return 0;
    int result = -1;

    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "VmSize:", 7) == 0) {
            result = parseLine(line);
            break;
        }
    }
    fclose(file);

    // the value we got is reported by linux in kB, so convert them:
    if (result > 0)
        result *= 1000;

    return result;
}

std::string MallocTagEngine::malloc_info()
{
    // get the malloc_info() stats; from the manpage:
    //  "The malloc_info() function is designed to address deficiencies in
    //   malloc_stats(3) and mallinfo(3)."
    // so it looks like the most modern way to get info out of glibc allocator
    char buf[16384];
    FILE* fp = fmemopen(buf, sizeof(buf), "w+");
    if (!fp) {
        // failed to get extra info, errno is set
        return "";
    }

    if (::malloc_info(0 /* options */, fp) != 0) {
        // failed to get extra info, errno is set
        return "";
    }

    fflush(fp);
    fclose(fp);

    return std::string(buf);
}

//------------------------------------------------------------------------------
// glibc overrides OF GLIBC basic malloc/new/free/delete functions:
//------------------------------------------------------------------------------

// set to 1 to debug if the actual application malloc()/free() are properly hooked or not
#define DEBUG_HOOKS 0

void __malloctag_track_allocation_from_glibc_override(MallocTagGlibcPrimitive_e type, size_t size)
{
    if (g_perthread_malloc_hook_active) {
        if (g_registry.has_main_thread_tree()) {
            // MallocTagEngine::init() has been invoked, good;
            // it means the tree for the main-thread is ready.
            // Let's check if the tree of _this_ thread has been initialized or not:
            if (UNLIKELY(!g_perthread_tree || !g_perthread_tree->is_ready())) {
                HookDisabler avoidInfiniteRecursionDueToMallocsInsideMalloc;

                g_perthread_tree = g_registry.register_secondary_thread_tree();
                // NOTE: if we're out of memory, g_perthread_tree might be nullptr
            }
            // else: this thread has its tree already available... nothing to do

            if (g_perthread_tree && g_perthread_tree->is_ready())
                g_perthread_tree->track_alloc_in_current_scope(type, size);
        } else
            // MallocTagEngine::init() has never been invoked... wait for that to happen
            g_bytes_allocated_before_init += size;
    }
    // else: hooks disabled: just behave as standard glibc malloc()
}

extern "C" {
void* malloc(size_t size)
{
    // always use the libc implementation to actually satisfy the malloc:
    void* result = __libc_malloc(size);
    if (result) {
        __malloctag_track_allocation_from_glibc_override(MTAG_GLIBC_PRIMITIVE_MALLOC, size);
    }
    // else: this software is out of memory... nothing to track

#if DEBUG_HOOKS
    // do logging
    {
        HookDisabler avoidInfiniteRecursionDueToMallocsInsideMalloc;
        printf("mtag_malloc_hook %zuB\n", size);
    }
#endif
    return result;
}

void free(void* __ptr) __THROW
{
    // always use the libc implementation to actually free memory:
    __libc_free(__ptr);
    if (g_perthread_malloc_hook_active)
        if (g_perthread_tree && g_perthread_tree->is_ready())
            g_perthread_tree->track_free_in_current_scope(MTAG_GLIBC_PRIMITIVE_FREE, 0);

#if DEBUG_HOOKS
    if (g_perthread_malloc_hook_active) // do logging
    {
        HookDisabler avoidInfiniteRecursionDueToMallocsInsideMalloc;
        printf("mtag_free_hook %p\n", __ptr);
    }
#endif
}

void* realloc(void* ptr, size_t newsize) __THROW
{
    // always use the libc implementation to actually satisfy the realloc:
    void* result = __libc_realloc(ptr, newsize);
    if (result) {
        __malloctag_track_allocation_from_glibc_override(MTAG_GLIBC_PRIMITIVE_REALLOC, newsize);
    }
    // else: this software is out of memory... nothing to track

#if DEBUG_HOOKS
    // do logging
    {
        HookDisabler avoidInfiniteRecursionDueToMallocsInsideMalloc;
        printf("mtag_malloc_hook %zuB\n", size);
    }
#endif
    return result;
}

void* calloc(size_t count, size_t eltsize) __THROW
{
    // always use the libc implementation to actually satisfy the calloc:
    void* result = __libc_calloc(count, eltsize);
    if (result) {
        __malloctag_track_allocation_from_glibc_override(MTAG_GLIBC_PRIMITIVE_CALLOC, count * eltsize);
    }
    // else: this software is out of memory... nothing to track

#if DEBUG_HOOKS
    // do logging
    {
        HookDisabler avoidInfiniteRecursionDueToMallocsInsideMalloc;
        printf("mtag_malloc_hook %zuB\n", size);
    }
#endif
    return result;
}

void* memalign(size_t alignment, size_t size) __THROW
{
    // always use the libc implementation to actually satisfy the calloc:
    void* result = __libc_memalign(alignment, size);
    if (result) {
        __malloctag_track_allocation_from_glibc_override(MTAG_GLIBC_PRIMITIVE_CALLOC, size);
    }
    // else: this software is out of memory... nothing to track

#if DEBUG_HOOKS
    // do logging
    {
        HookDisabler avoidInfiniteRecursionDueToMallocsInsideMalloc;
        printf("mtag_malloc_hook %zuB\n", size);
    }
#endif
    return result;
}

void* valloc(size_t size) __THROW
{
    // always use the libc implementation to actually satisfy the calloc:
    void* result = __libc_valloc(size);
    if (result) {
        __malloctag_track_allocation_from_glibc_override(MTAG_GLIBC_PRIMITIVE_CALLOC, size);
    }
    // else: this software is out of memory... nothing to track

#if DEBUG_HOOKS
    // do logging
    {
        HookDisabler avoidInfiniteRecursionDueToMallocsInsideMalloc;
        printf("mtag_malloc_hook %zuB\n", size);
    }
#endif
    return result;
}

void* pvalloc(size_t size) __THROW
{
    // always use the libc implementation to actually satisfy the calloc:
    void* result = __libc_pvalloc(size);
    if (result) {
        __malloctag_track_allocation_from_glibc_override(MTAG_GLIBC_PRIMITIVE_CALLOC, size);
    }
    // else: this software is out of memory... nothing to track

#if DEBUG_HOOKS
    // do logging
    {
        HookDisabler avoidInfiniteRecursionDueToMallocsInsideMalloc;
        printf("mtag_malloc_hook %zuB\n", size);
    }
#endif
    return result;
}

}; // extern C
