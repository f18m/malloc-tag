/*
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
#include "fmpool.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <sys/prctl.h>

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAX_THREADS 128
#define MAX_SITENAME_LEN 16 // must be at least 16 bytes long due to use in prctl(PR_GET_NAME)
#define WEIGHT_MULTIPLIER 10000
#define MAX_CHILDREN_PER_NODE 16

#define UNLIKELY(x) __builtin_expect((x), 0)

//------------------------------------------------------------------------------
// glibc original implementation
//------------------------------------------------------------------------------

extern "C" {
// these __libc_* functions are the actual glibc implementation:
void* __libc_malloc(size_t size);
void __libc_free(void*);

// malloctag globals that need to be declared at top:
thread_local bool g_perthread_malloc_hook_active = true;
std::atomic<size_t> g_bytes_allocated_before_init; // this accounts for ALL mallocs done by ALL threads before init
};

//------------------------------------------------------------------------------
// Utils
//------------------------------------------------------------------------------

class GraphVizUtils {
public:
    static void append_graphviz_node(std::string& out, const std::string& nodeName, const std::string& label)
    {
        out += nodeName + " [label=\"" + label + "\"]\n";
    }

    static std::string pretty_print_bytes(size_t bytes)
    {
        // NOTE: we convert to kilo/mega/giga (multiplier=1000) not to kibi/mebi/gibi (multiplier=1024) bytes !!!
        if (bytes < 1000ul)
            return std::to_string(bytes) + "B";
        else if (bytes < 1000000ul)
            return std::to_string(bytes / 1000) + "kB";
        else if (bytes < 1000000000ul)
            return std::to_string(bytes / 1000000ul) + "MB";
        else
            return std::to_string(bytes / 1000000000ul) + "GB";
    }
};

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

//------------------------------------------------------------------------------
// MallocTreeNode_t
//------------------------------------------------------------------------------

/// Node in the malloc tree structure.
/// These nodes are created by the application using the MallocTagScope helper class
/// in sensible parts of their code, to enable malloc tree profiling.
///
/// MallocTreeNode_t nodes track both the memory they incur directly
/// (m_nBytesDirect) but more importantly, the total memory allocated by
/// themselves and any of their children (m_nBytes) and also its percentage weigth (m_nWeight).
typedef struct MallocTreeNode_s {
    size_t m_nBytes; ///< Allocated bytes by this or descendant nodes.
    size_t m_nBytesDirect; ///< Allocated bytes (only for this node).
    size_t m_nAllocations; ///< The number of allocations for this node.
    unsigned int m_nTreeLevel; ///< How deep is located this node in the tree?
    size_t m_nWeight; ///< Weight of this node expressed as WEIGHT_MULTIPLIER*(m_nBytes/TOTAL_TREE_BYTES)
    std::array<char, MAX_SITENAME_LEN> m_siteName; ///< Site name, NUL terminated
    std::array<MallocTreeNode_s*, MAX_CHILDREN_PER_NODE> m_pChildren; ///< Children nodes.
    unsigned int m_nChildrens; ///< Number of valid children pointers in m_pChildren[]
    MallocTreeNode_s* m_pParent; ///< Pointer to parent node; NULL if this is the root node

    void init(MallocTreeNode_s* parent)
    {
        m_nBytes = 0;
        m_nBytesDirect = 0;
        m_nAllocations = 0;
        m_nTreeLevel = parent ? parent->m_nTreeLevel + 1 : 0;
        m_siteName[0] = '\0';
        m_nChildrens = 0;
        m_pParent = parent;
    }

    void set_sitename_to_shlib_name_from_func_pointer(void* funcpointer)
    {
        Dl_info address_info;
        if (dladdr(funcpointer, &address_info) == 0 || address_info.dli_fname == nullptr) {
            strncpy(&m_siteName[0], "UnknownSharedLib", MAX_SITENAME_LEN);
        } else
            strncpy(&m_siteName[0], address_info.dli_fname,
                std::min(strlen(address_info.dli_fname), (size_t)MAX_SITENAME_LEN));

        // FIXME: should we free the pointers inside "address_info"??
    }
    void set_sitename_to_threadname() { prctl(PR_GET_NAME, &m_siteName[0], 0, 0); }
    void set_sitename(const char* sitename)
    {
        strncpy(&m_siteName[0], sitename, std::min(strlen(sitename), (size_t)MAX_SITENAME_LEN));
    }
    bool link_new_children(MallocTreeNode_s* new_child)
    {
        if (m_nChildrens < MAX_CHILDREN_PER_NODE) {
            m_pChildren[m_nChildrens++] = new_child;
            return true;
        }
        return false;
    }

    MallocTreeNode_s* get_child_by_name(const char* name) const
    {
        size_t nchars = std::min(strlen(name), (size_t)MAX_SITENAME_LEN);
        for (unsigned int i = 0; i < m_nChildrens; i++)
            if (strncmp(&m_pChildren[i]->m_siteName[0], name, nchars) == 0)
                return m_pChildren[i];
        return NULL;
    }

    void track_malloc(size_t nBytes)
    {
        m_nBytesDirect += nBytes;
        m_nAllocations++;
    }

    float get_weight_percentage() const
    {
        // to make this tree node more compact, we don't store a floating point directly;
        // rather we store a percentage in [0..1] range multiplied by WEIGHT_MULTIPLIER
        return 100.0f * (float)m_nWeight / (float)WEIGHT_MULTIPLIER;
    }

    std::string get_weight_percentage_str() const
    {
        char ret[16];
        // ensure only 2 digits of accuracy:
        snprintf(ret, 15, "%.2f", get_weight_percentage());
        return std::string(ret);
    }

    std::string get_node_name() const
    {
        // to make this tree node more compact, we don't store a std::string which might
        // trigger unwanted memory allocations (to resize itself), rather we store a
        // fixed-size array of chars:
        return std::string(&m_siteName[0], strlen(&m_siteName[0]));
    }

    void collect_json_stats_recursively(std::string& out)
    {
        // each node is a JSON object
        out += "\"" + get_node_name() + "\":{";
        out += "\"nBytes\": " + std::to_string(m_nBytes) + ",";
        out += "\"nBytesDirect\": " + std::to_string(m_nBytesDirect) + ",";
        out += "\"nWeightPercentage\": " + get_weight_percentage_str() + ",";
        out += "\"nAllocations\": " + std::to_string(m_nAllocations) + ",";
        out += "\"nestedScopes\": { ";
        for (unsigned int i = 0; i < m_nChildrens; i++) {
            m_pChildren[i]->collect_json_stats_recursively(out);
            if (i < m_nChildrens - 1)
                // there's another node to dump:
                out += ",";
        }
        out += "}}"; // close childrenNodes + the whole node object
    }

    void collect_graphviz_dot_output(std::string& out)
    {
        std::string thisNodeName = get_node_name();

        // write a description of this node:
        std::string thisNodeLabel;
        if (m_pParent == NULL)
            // for root node, provide a more verbose label
            thisNodeLabel = "thread=" + thisNodeName + "\\n" + get_weight_percentage_str() + "%" + "\\n"
                + GraphVizUtils::pretty_print_bytes(m_nBytes);
        else
            thisNodeLabel = thisNodeName + "\\n" + get_weight_percentage_str() + "%";
        GraphVizUtils::append_graphviz_node(out, thisNodeName, thisNodeLabel);

        // write all the connections between this node and its children:
        for (unsigned int i = 0; i < m_nChildrens; i++) {
            out += thisNodeName + " -> " + m_pChildren[i]->get_node_name() + "\n";
        }

        // now recurse into each children:
        for (unsigned int i = 0; i < m_nChildrens; i++)
            m_pChildren[i]->collect_graphviz_dot_output(out);
    }

    size_t compute_bytes_totals_recursively() // returns total bytes accumulated by this node
    {
        // postorder traversal of a tree:

        // first of all, traverse all children subtrees:
        size_t accumulated_bytes = 0;
        for (unsigned int i = 0; i < m_nChildrens; i++)
            accumulated_bytes += m_pChildren[i]->compute_bytes_totals_recursively();

        // finally "visit" this node, updating the bytes count:
        m_nBytes = accumulated_bytes + m_nBytesDirect;
        return m_nBytes;
    }

    void compute_node_weights_recursively(size_t rootNodeTotalBytes)
    {
        // weighr is defined as
        m_nWeight = WEIGHT_MULTIPLIER * m_nBytes / rootNodeTotalBytes;
        for (unsigned int i = 0; i < m_nChildrens; i++)
            m_pChildren[i]->compute_node_weights_recursively(rootNodeTotalBytes);
    }

} MallocTreeNode_t;

// a memory pool of MallocTreeNode_t allows us to AVOID memory allocation during the program execution, after
// the initial memory pool initialization
FMPOOL_INIT(MallocTreeNode_t)

//------------------------------------------------------------------------------
// MallocTree_t
//------------------------------------------------------------------------------
typedef struct MallocTree_s {
    fmpool_t(MallocTreeNode_t) * m_pNodePool = NULL;
    MallocTreeNode_t* m_pRootNode = NULL;
    MallocTreeNode_t* m_pCurrentNode = NULL;

    // last push status:
    unsigned int m_nPushNodeFailures = 0;
    bool m_bLastPushWasSuccessful = false;

    // tree status:
    unsigned int m_nTreeNodesInUse = 0;
    unsigned int m_nTreeLevels = 0;

    // tree limits:
    unsigned int m_nMaxTreeNodes = MTAG_DEFAULT_MAX_TREE_NODES;
    unsigned int m_nMaxTreeLevels = MTAG_DEFAULT_MAX_TREE_LEVELS;

    bool init(size_t max_tree_nodes, size_t max_tree_levels) // triggers some MEMORY ALLOCATION
    {
        assert(m_pNodePool == nullptr);

        // initialize the memory pool of tree nodes
        m_pNodePool = fmpool_create(MallocTreeNode_t, max_tree_nodes);
        if (!m_pNodePool)
            return false;

        // init the "current node" pointer to have the same name of the
        m_pRootNode = fmpool_get(MallocTreeNode_t, m_pNodePool);
        assert(m_pRootNode);
        m_nTreeNodesInUse++;
        m_pRootNode->init(NULL); // this is the tree root node
        // m_pRootNode->set_sitename_to_shlib_name_from_func_pointer(caller);
        m_pRootNode->set_sitename_to_threadname();

        m_nTreeLevels = 0;
        m_nMaxTreeNodes = max_tree_nodes;
        m_nMaxTreeLevels = max_tree_levels;

        m_pCurrentNode = m_pRootNode;

        return true;
    }

    bool init(MallocTree_s* main_thread_tree)
    {
        // all secondary threads will create identical trees
        // (this might change if I see the memory usage of malloctag is too high)
        return init(main_thread_tree->m_nMaxTreeNodes, main_thread_tree->m_nMaxTreeLevels);
    }

    bool is_ready() { return m_pNodePool != NULL && m_pRootNode != NULL && m_pCurrentNode != NULL; }

    void push_new_node(const char* name) // must be malloc-free
    {
        if (UNLIKELY(m_pCurrentNode->m_nTreeLevel == m_nMaxTreeLevels)) {
            // reached max depth level... cannot push anymore
            m_nPushNodeFailures++;
            m_bLastPushWasSuccessful = false;
            return;
        }

        MallocTreeNode_t* n = m_pCurrentNode->get_child_by_name(name);
        if (n) {
            // this branch of the tree already exists, just move the cursor:
            m_pCurrentNode = n;
            m_bLastPushWasSuccessful = true;
            return;
        }

        // this branch of the tree needs to be created:
        n = fmpool_get(MallocTreeNode_t, m_pNodePool);
        if (UNLIKELY(!n)) {
            // memory pool is full... memory profiling results will be INCOMPLETE and possibly MISLEADING:
            m_nPushNodeFailures++;
            m_bLastPushWasSuccessful = false;
            return;
        }

        m_nTreeNodesInUse++; // successfully obtained a new node from the mempool
        n->init(m_pCurrentNode);
        n->set_sitename(name);
        if (!m_pCurrentNode->link_new_children(n)) {
            // failed to link current node: release node back to the pool
            m_nTreeNodesInUse--;
            fmpool_free(MallocTreeNode_t, n, m_pNodePool);

            // and record this failure:
            m_nPushNodeFailures++;
            m_bLastPushWasSuccessful = false;
            return;
        }

        // new node ready, move the cursor:
        m_pCurrentNode = n;
        m_bLastPushWasSuccessful = true;
        m_nTreeLevels = std::max(m_nTreeLevels, m_pCurrentNode->m_nTreeLevel);
    }

    void pop_last_node() // must be malloc-free
    {
        if (m_bLastPushWasSuccessful) {
            MallocTreeNode_t* n = m_pCurrentNode->m_pParent;
            assert(n); // if n == NULL it means m_pCurrentNode is pointing to the tree root... cannot pop... this is a
                       // logical mistake...
            m_pCurrentNode = n;
        }
        // else: the node pointer has not been moved by last push_new_node() so we don't need to really pop the node
        // pointer
    }

    size_t get_memory_usage() const { return fmpool_mem_usage(MallocTreeNode_t, m_pNodePool); }

    void collect_stats_recursively(std::string& out, MallocTagOutputFormat_e format)
    {
        switch (format) {
        case MTAG_OUTPUT_FORMAT_JSON:
            out += "{";
            out += "\"nTreeLevels\": " + std::to_string(m_nTreeLevels) + ",";
            out += "\"nTreeNodesInUse\": " + std::to_string(m_nTreeNodesInUse) + ",";
            out += "\"nPushNodeFailures\": " + std::to_string(m_nPushNodeFailures) + ",";
            out += "\"nMallocTagSelfUsageBytes\": " + std::to_string(get_memory_usage()) + ",";
            out += "\"nBytesAllocBeforeInit\": " + std::to_string(g_bytes_allocated_before_init) + ",";
            m_pRootNode->collect_json_stats_recursively(out);
            out += "}";
            break;

        case MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT:
            // see https://graphviz.org/doc/info/lang.html
            out += "digraph MallocTree {\n";
            m_pRootNode->collect_graphviz_dot_output(out);

            // add a few nodes "external" to the tree:
            GraphVizUtils::append_graphviz_node(out, "__before_init_node__",
                "Memory Allocated\\nBefore MallocTag Init\\n"
                    + GraphVizUtils::pretty_print_bytes(g_bytes_allocated_before_init));
            GraphVizUtils::append_graphviz_node(out, "__malloctag_self_memory__",
                "Memory Allocated\\nBy MallocTag itself\\n" + GraphVizUtils::pretty_print_bytes(get_memory_usage()));
            out += "}";

            break;
        }
    }

    void compute_bytes_totals_recursively()
    {
        // NOTE: order is important:

        // STEP1: compute "bytes total" across the whole tree
        m_pRootNode->compute_bytes_totals_recursively();

        // STEP2: compute node weigth across the whole tree:
        m_pRootNode->compute_node_weights_recursively(m_pRootNode->m_nBytes);
    }
} MallocTree_t;

// more globals related:
thread_local MallocTree_t g_perthread_tree;
MallocTree_t* g_global_registry_malloc_trees[MAX_THREADS];
std::atomic_uint g_global_registry_size;

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
        size_t reservedIdx = g_global_registry_size.fetch_add(1);
        g_global_registry_malloc_trees[reservedIdx] = &g_perthread_tree;
    }

    return result;
}

std::string MallocTagEngine::collect_stats(MallocTagOutputFormat_e format)
{
    HookDisabler doNotAccountCollectStatMemoryUsage;

    // reserve enough space inside the output string:
    std::string ret;
    ret.reserve(4096);

    // now traverse the tree collecting stats:
    g_perthread_tree.compute_bytes_totals_recursively();
    g_perthread_tree.collect_stats_recursively(ret, format);

    return ret;
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
            if (g_global_registry_size > 0) {
                // MallocTagEngine::init() has been invoked, good.
                // It means the tree for the main-thread is ready.
                // Let's check if the tree of _this_ thread has been initialized or not:
                if (UNLIKELY(!g_perthread_tree.is_ready()))
                    g_perthread_tree.init(g_global_registry_malloc_trees[0]);
                g_perthread_tree.m_pCurrentNode->track_malloc(size);
            } else
                g_bytes_allocated_before_init += size;
        }

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
