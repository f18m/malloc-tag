/*
 * Malloc hooks that implement a low-overhead memory profiler
 * This is the PUBLIC header that applications using malloc-tag should include
 *
 * Inspired by:
 *  - Pixar TfMallocTag tool, see https://openusd.org/dev/api/page_tf__malloc_tag.html
 *
 * Author: fmontorsi
 * Created: Aug 2023
 * License: Apache license
 *
 */

#pragma once

//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------

#include <malloc.h> // provides prototypes for malloc()/free()/etc
#include <map>
#include <string>

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

/*
    For most of the MallocTagEngine functions will accept either explicit parameters or, if these are not provided,
    the values will be read from the following environment variables.
    This is handy for quick tests since there is no need to recompile the software: you just change a value of an env
   var and relaunch the software.
*/
#define MTAG_STATS_OUTPUT_JSON_ENV "MTAG_STATS_OUTPUT_JSON"
#define MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV "MTAG_STATS_OUTPUT_GRAPHVIZ_DOT"

/*
    Each MallocTree used to track allocations done by a specific thread context will be able to handle only a
    certain number of "memory allocation scopes", since each scope translates to 1node inside the tree.
    By default a MallocTree will support up to MTAG_DEFAULT_MAX_TREE_NODES nodes.
    There is no dynamic growth of the MallocTree in order to avoid corner cases where e.g. MallocTagScope tries
    to push a crazy number of nodes in the tree and malloc-tag ends up eating a lot of memory. malloc-tag is
    designed to be a "lightweight" memory profiler and its own memory consumption should be minimal.
*/
#define MTAG_DEFAULT_MAX_TREE_NODES 256

/*
    The max number of tree levels is a safety threshold to avoid corner cases like e.g. in case a MallocTagScope
    is used inside a recursive function that invokes itself thousands of time.
    This limit is artificial: the only true limit for malloc-tag is the number of pre-allocated nodes
    (see MTAG_DEFAULT_MAX_TREE_NODES).
*/
#define MTAG_DEFAULT_MAX_TREE_LEVELS 256

/*
    Output options supported by MallocTagEngine::write_stats()
*/
#define MTAG_GRAPHVIZ_OPTION_UNIQUE_TREE "uniquetree"

//------------------------------------------------------------------------------
// glibc overrides
//------------------------------------------------------------------------------

extern "C" {
/* the malloc()/free() interceptor defined by this library
   NOTE that GNU libc will define all these functions as "weak"
   symbols so that they can be overridden by any other library exporting
   a non-weak symbol:

   nm -C /usr/lib64/libc.so.6  | grep -i alloc | grep " W "
    000000000009d360 W aligned_alloc
    000000000009d420 W calloc
    0000000000125620 W fallocate
    000000000009e130 W malloc_info
    000000000009dcf0 W malloc_stats
    000000000009d810 W malloc_trim
    000000000009dae0 W malloc_usable_size
    000000000009d3b0 W pvalloc
    000000000009fae0 W reallocarray
    000000000009d370 W valloc
*/
void* malloc(size_t size) __attribute_malloc__;
void free(void* __ptr) __THROW;

void* realloc(void* ptr, size_t newsize) __THROW;
void* calloc(size_t count, size_t eltsize) __THROW;
void* memalign(size_t alignment, size_t size) __THROW __attribute_malloc__;
void* valloc(size_t __size) __THROW __attribute_malloc__;
void* pvalloc(size_t __size) __THROW __attribute_malloc__;
};

//------------------------------------------------------------------------------
// malloc_tag public API
//------------------------------------------------------------------------------

enum MallocTagOutputFormat_e {
    MTAG_OUTPUT_FORMAT_JSON,
    MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT,

    MTAG_OUTPUT_FORMAT_ALL
};

typedef std::map<std::string, size_t> MallocTagStatMap_t;

class MallocTagEngine {
public:
    MallocTagEngine() { }

    // The main API to initialize malloc-tag.
    // Call this function from the main thread, possibly as first thing inside the "main()" function
    // and before your software starts launching threads.
    static bool init( // fn
        size_t max_tree_nodes = MTAG_DEFAULT_MAX_TREE_NODES, // fn
        size_t max_tree_levels = MTAG_DEFAULT_MAX_TREE_LEVELS);

    // The main API to collect all results in JSON/GRAPHVIZ-DOT format
    // NOTE: invoking this function will indeed trigger several memory allocation on its own (!!!);
    //       such memory allocations are EXCLUDED from the stats collected by MallocTagEngine
    static std::string collect_stats(
        MallocTagOutputFormat_e format, const std::string& output_options = MTAG_GRAPHVIZ_OPTION_UNIQUE_TREE);

    // Another API to collect all stats in form of a flat map/dictionary.
    // This API provides a machine-friendly structured view on all the memory allocation stats for all the trees
    /* Suggested way to explore the contents of the returned std::map is:
        for (const auto& it : MallocTagEngine::collect_stats())
            std::cout << it.first << "=" << it.second << std::endl;

       Example:
            .nTrees=1
            tid1734695_minimal_tcm.nBytesSelf=1024
            tid1734695_minimal_tcm.nBytesTotal=0
            tid1734695_minimal_tcm.nCallsTo_calloc=0
            tid1734695_minimal_tcm.nCallsTo_free=0
            tid1734695_minimal_tcm.nCallsTo_malloc=1
            tid1734695_minimal_tcm.nCallsTo_realloc=0
    */
    static MallocTagStatMap_t collect_stats();

    // Returns the prefix of the keys contained in the MallocTagStatMap_t that are associated with a particular
    // thread of this application. If the provided thread_id is zero, the prefix for current thread is returned.
    // Concatenate the returned string with "." and the name of a KPI to access MallocTagStatMap_t contents.
    static std::string get_stat_key_prefix_for_thread(pid_t thread_id = 0);

    // Write memory profiler stats into a file on disk;
    // if an empty string is passed, the full path will be taken from the environment variable
    // MTAG_STATS_OUTPUT_JSON_ENV or MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV, depending on the "format" argument.
    // This API is a shorthand for collect_stats() + some file opening/writing operations.
    static bool write_stats(MallocTagOutputFormat_e format = MTAG_OUTPUT_FORMAT_ALL, const std::string& fullpath = "",
        const std::string& output_options = MTAG_GRAPHVIZ_OPTION_UNIQUE_TREE);

    // Get all virtual memory "associated" by Linux to this process.
    // This is a Linux-specific utility.
    // This utility function has nothing to do with malloctag profiler but it can be used to get the
    // OS-view of memory usage and see if it roughly matches with malloctag-reported results.
    // The total memory allocations intercepted by malloctag will never match exactly the VIRT/RSS memory
    // reported by Linux for a number of reasons:
    //  * malloctag only knows when new threads are spawned (they are detected by the time they invoke the first malloc)
    //    but it is not notified that a thread has completed, so it cannot remove the "tree" associated with that thread
    //    from the total tracked memory bytes;
    //  * malloctag is not aware about the internal-allocator (e.g. glibc dlmalloc) overhead and
    //    logic to acquire memory from the OS, specially the glibc per-thread arena mechanism.
    //    malloctag has some heuristic to estimate the glibc per-thread consumption of VIRTUAL memory but
    //    these heuristics are far from perfect;
    //  * some memory allocations might happen via alternative methods compared to malloc()/new,
    //    e.g. invoking directly mmap() or sbrk() syscalls; these are not intercepted/accounted-for by malloctag.
    // Due to all considerations above, generally speaking the total memory tracked by malloc-tag
    // will be HIGHER than the VIRT memory reported by the kernel.
    //
    // Btw to understand the difference between VmSize/VmRSS and other memory measurements reported by
    // the Linux kernel, check e.g.
    //  https://web.archive.org/web/20120520221529/http://emilics.com/blog/article/mconsumption.html
    // Please note that this function will allocate memory itself (!!!)
    static size_t get_linux_vmsize_in_bytes();

    // Get glibc internal allocator stats in std::string form.
    // This utility function has nothing to do with malloctag profiler and uses the glibc ::malloc_info()
    // to acquire these stats which will be in XML format and that may vary across different glibc versions, see
    //  https://man7.org/linux/man-pages/man3/malloc_info.3.html
    // Please note that this function will allocate memory itself (!!!)
    static std::string malloc_info();
};

class MallocTagScope {
public:
    // advance the per-thread cursor inside the malloc tree by 1 more level, adding the "tag_name" SCOPE
    MallocTagScope(const char* tag_name);

    // advance the per-thread cursor inside the malloc tree by 1 more level, adding the
    //   class_name::function_name
    // SCOPE
    MallocTagScope(const char* class_name, const char* function_name);

    // pop by 1 level the current per-thread cursor
    ~MallocTagScope();

private:
    bool m_bLastPushWasSuccessful;
};
