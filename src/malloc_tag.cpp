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
// Constants
//------------------------------------------------------------------------------

#define UNLIKELY(x) __builtin_expect((x), 0)

//------------------------------------------------------------------------------
// glibc original implementation
//------------------------------------------------------------------------------

extern "C" {
// these __libc_* functions are the actual glibc implementation:
void* __libc_malloc(size_t size);
void __libc_free(void*);
};

// malloctag globals:
thread_local bool g_perthread_malloc_hook_active = true;
std::atomic<size_t> g_bytes_allocated_before_init; // this accounts for ALL mallocs done by ALL threads before init

// the main global per-thread malloc tree:
thread_local MallocTree_t g_perthread_tree;

//------------------------------------------------------------------------------
// Utils
//------------------------------------------------------------------------------

class HookDisabler {
public:
    HookDisabler()
    {
        m_prev_state = g_perthread_malloc_hook_active;
        g_perthread_malloc_hook_active = false;
    }
    ~HookDisabler() { g_perthread_malloc_hook_active = m_prev_state; }

private:
    bool m_prev_state = false;
};

// the global registry for all threads; this is NOT thread-specific
MallocTreeRegistry g_registry;

//------------------------------------------------------------------------------
// MallocTagScope
//------------------------------------------------------------------------------

MallocTagScope::MallocTagScope(const char* tag_name)
{
    // advance the per-thread cursor inside the malloc tree by 1 more level, adding the "tag_name" level
    // VERY IMPORTANT: all code running in this function must be malloc-free
    // if (g_perthread_tree.is_ready())
    assert(g_perthread_tree.is_ready()); // it's a logical mistake to use MallocTagScope before MallocTagEngine::init()
    g_perthread_tree.push_new_node(tag_name);
}

MallocTagScope::~MallocTagScope()
{
    // pop by 1 level the current per-thread cursor
    // VERY IMPORTANT: all code running in this function must be malloc-free
    // if (g_perthread_tree.is_ready())
    assert(g_perthread_tree.is_ready()); // it's a logical mistake to use MallocTagScope before MallocTagEngine::init()
    g_perthread_tree.pop_last_node();
}

//------------------------------------------------------------------------------
// MallocTagEngine
//------------------------------------------------------------------------------

bool MallocTagEngine::init(size_t max_tree_nodes, size_t max_tree_levels)
{
    if (UNLIKELY(g_perthread_tree.is_ready()))
        return true; // invoking twice?

    bool result;
    {
        HookDisabler doNotAccountSelfMemoryInCurrentScope;
        result = g_perthread_tree.init(max_tree_nodes, max_tree_levels);

        // register the "first tree" in the registry:
        // this will "unblock" the creation of malloc trees for all other threads, see malloc() logic
        g_registry.register_tree(&g_perthread_tree);
    }

    return result;
}

std::string MallocTagEngine::collect_stats(MallocTagOutputFormat_e format)
{
    HookDisabler doNotAccountCollectStatMemoryUsage;

    // reserve enough space inside the output string:
    std::string stats_str;
    stats_str.reserve(4096);

    // now traverse the tree collecting stats:
    g_perthread_tree.compute_bytes_totals_recursively();

    switch (format) {
    case MTAG_OUTPUT_FORMAT_JSON:
        stats_str += "{";
        stats_str += "\"nBytesAllocBeforeInit\": " + std::to_string(g_bytes_allocated_before_init) + ",";
        stats_str += "\"nMallocTagSelfUsageBytes\": " + std::to_string(g_registry.get_total_memusage()) + ",";
        g_perthread_tree.collect_stats_recursively(stats_str, format);
        stats_str += "}";
        break;

    case MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT:
        // see https://graphviz.org/doc/info/lang.html
        stats_str += "digraph MallocTree {\n";
        g_perthread_tree.collect_stats_recursively(stats_str, format);

        // add a few nodes "external" to the tree:
        GraphVizUtils::append_graphviz_node(stats_str, "__before_init_node__",
            "Memory Allocated\\nBefore MallocTag Init\\n"
                + GraphVizUtils::pretty_print_bytes(g_bytes_allocated_before_init));
        GraphVizUtils::append_graphviz_node(stats_str, "__malloctag_self_memory__",
            "Memory Allocated\\nBy MallocTag itself\\n"
                + GraphVizUtils::pretty_print_bytes(g_registry.get_total_memusage()));
        stats_str += "}";

        break;
    }

    return stats_str;
}

bool MallocTagEngine::write_stats_on_disk(MallocTagOutputFormat_e format, const std::string& fullpath)
{
    bool bwritten = false;
    HookDisabler doNotAccountCollectStatMemoryUsage;

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
        }
    }

    std::ofstream stats_file(fpath);
    if (stats_file.is_open()) {
        stats_file << collect_stats(format) << std::endl;
        bwritten = true;
        stats_file.close();
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

size_t MallocTagEngine::get_linux_rss_mem_usage_in_bytes()
{
    FILE* file = fopen("/proc/self/status", "r");
    if (!file)
        return 0;
    int result = -1;
    char line[128];

    while (fgets(line, 128, file) != NULL) {
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

//------------------------------------------------------------------------------
// glibc overrides OF GLIBC basic malloc/new/free/delete functions:
//------------------------------------------------------------------------------

// set to 1 to debug if the actual application malloc()/free() are properly hooked or not
#define DEBUG_HOOKS 0

extern "C" {
void* malloc(size_t size)
{
    void* caller = __builtin_return_address(0);

    // always use the libc implementation to actually satisfy the malloc:
    void* result = __libc_malloc(size);
    if (g_perthread_malloc_hook_active) {
        if (result) {
            if (g_registry.has_main_thread_tree()) {
                // MallocTagEngine::init() has been invoked, good.
                // It means the tree for the main-thread is ready.
                // Let's check if the tree of _this_ thread has been initialized or not:
                if (UNLIKELY(!g_perthread_tree.is_ready())) {
                    HookDisabler doNotAccountSelfMemoryInCurrentScope;
                    if (g_perthread_tree.init(g_registry.get_main_thread_tree()))
                        g_registry.register_tree(&g_perthread_tree);
                    // else: tree could not be initialized... probably we're out of memory... give up
                }
                // else: this thread has its tree already ready... nothing to do

                if (g_perthread_tree.is_ready())
                    g_perthread_tree.m_pCurrentNode->track_malloc(size);

            } else
                // MallocTagEngine::init() has never been invoked... wait for that to happen
                g_bytes_allocated_before_init += size;
        }
        // else: this software is out of memory... nothing to track

#if DEBUG_HOOKS
        // do logging
        {
            HookDisabler avoidInfiniteRecursion;
            printf("mtag_malloc_hook %zuB\n", size);
        }
#endif
    }

    return result;
}

void free(void* __ptr) __THROW
{
    // always use the libc implementation to actually free memory:
    __libc_free(__ptr);

#if DEBUG_HOOKS
    if (g_perthread_malloc_hook_active) // do logging
    {
        HookDisabler avoidInfiniteRecursion;
        printf("mtag_free_hook %p\n", __ptr);
    }
#endif
}
}; // extern C
