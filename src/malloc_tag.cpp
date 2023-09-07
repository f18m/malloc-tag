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
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <sys/prctl.h>

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

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
int g_malloc_hook_active = 1; // FIXME: this should probably be per-thread!
size_t g_bytes_allocated_before_init = 0;
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
        out += thisNodeName + " [label=\"" + thisNodeName + "\\n" + get_weight_percentage_str() + "%";
        if (m_pParent == NULL)
            out += "\\n" + std::to_string(m_nBytes) + "B";
        out += "\"]\n";

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
        m_nMaxTreeLevels = max_tree_levels;

        m_pCurrentNode = m_pRootNode;

        return true;
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
            if (n)
                m_pCurrentNode = n;
            // else: we are already at the tree root... cannot pop... this is a logical mistake... FIXME: assert?
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

MallocTree_t g_perthread_tree; // FIXME: must be per-thread

//------------------------------------------------------------------------------
// MallocTagScope
//------------------------------------------------------------------------------

MallocTagScope::MallocTagScope(const char* tag_name)
{
    // advance the per-thread cursor inside the malloc tree by 1 more level, adding the "tag_name" level
    // VERY IMPORTANT: all code running in this function must be malloc-free
    if (g_perthread_tree.is_ready())
        g_perthread_tree.push_new_node(tag_name);
}

MallocTagScope::~MallocTagScope()
{
    // pop by 1 level the current per-thread cursor
    // VERY IMPORTANT: all code running in this function must be malloc-free
    if (g_perthread_tree.is_ready())
        g_perthread_tree.pop_last_node();
}

//------------------------------------------------------------------------------
// mtag hooks
//------------------------------------------------------------------------------

#define DEBUG_HOOKS 0

void* mtag_malloc_hook(size_t size, void* caller)
{
    void* result = __libc_malloc(size);

    if (result) {
        if (g_perthread_tree.is_ready())
            g_perthread_tree.m_pCurrentNode->track_malloc(size);
        else
            g_bytes_allocated_before_init += size;
    }

#if DEBUG_HOOKS
    // do logging
    g_malloc_hook_active = 0; // deactivate hooks for logging
    printf("mtag_malloc_hook %zuB\n", size);
    g_malloc_hook_active = 1; // reactivate hooks
#endif

    return result;
}

void mtag_free_hook(void* __ptr)
{
    __libc_free(__ptr);

#if DEBUG_HOOKS
    // do logging
    g_malloc_hook_active = 0; // deactivate hooks for logging
    printf("mtag_free_hook %p\n", __ptr);
    g_malloc_hook_active = 1; // reactivate hooks
#endif
}

//------------------------------------------------------------------------------
// MallocTagEngine
//------------------------------------------------------------------------------

bool MallocTagEngine::init(size_t max_tree_nodes, size_t max_tree_levels)
{
    if (UNLIKELY(g_perthread_tree.is_ready()))
        return true; // invoking twice?

    g_malloc_hook_active = 0; // deactivate hooks during initialization
    bool result = g_perthread_tree.init(max_tree_nodes, max_tree_levels);
    g_malloc_hook_active = 1; // reactivate hooks

    return result;
}

std::string MallocTagEngine::collect_stats(MallocTagOutputFormat_e format)
{
    g_malloc_hook_active = 0; // deactivate hooks for stat collection

    // reserve enough space inside the output string:
    std::string ret;
    ret.reserve(4096);

    // now traverse the tree collecting stats:
    g_perthread_tree.compute_bytes_totals_recursively();
    g_perthread_tree.collect_stats_recursively(ret, format);

    g_malloc_hook_active = 1; // reactivate hooks
    return ret;
}

bool MallocTagEngine::write_stats_on_disk(MallocTagOutputFormat_e format, const std::string& fullpath)
{
    bool bwritten = false;

    g_malloc_hook_active = 0; // deactivate hooks for stat collection

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

    g_malloc_hook_active = 1; // reactivate hooks
    return bwritten;
}

//------------------------------------------------------------------------------
// glibc overrides OF GLIBC basic malloc/new/free/delete functions:
//------------------------------------------------------------------------------

extern "C" {
void* malloc(size_t size)
{
    void* caller = __builtin_return_address(0);
    if (g_malloc_hook_active)
        return mtag_malloc_hook(size, caller);
    else
        return __libc_malloc(size);
}

void free(void* __ptr) __THROW
{
    if (g_malloc_hook_active)
        mtag_free_hook(__ptr);
    else
        __libc_free(__ptr);
}
};
// extern C
