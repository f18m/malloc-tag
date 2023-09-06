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
#define MAX_CHILDREN_PER_NODE 4
#define MAX_TREE_NODES 256
#define MAX_TREE_LEVELS 4
#define WEIGHT_MULTIPLIER 10000

//------------------------------------------------------------------------------
// glibc original implementation
//------------------------------------------------------------------------------

extern "C" {
// these __libc_* functions are the actual glibc implementation:
void* __libc_malloc(size_t size);
void __libc_free(void*);
int g_malloc_hook_active = 1; // FIXME: this should probably be per-thread!
};

//------------------------------------------------------------------------------
// MallocTreeNode_t
//------------------------------------------------------------------------------

/// Node in the call tree structure.
///
/// MallocTreeNode_t nodes track both the memory they incur directly
/// (nBytesDirect) but more importantly, the total memory allocated by
/// themselves and any of their children (nBytes).  The name of a
/// node (siteName) corresponds to the tag name of the final tag in
/// the path.
typedef struct MallocTreeNode_s {
    size_t m_nBytes; ///< Allocated bytes by this or descendant nodes.
    size_t m_nBytesDirect; ///< Allocated bytes (only for this node).
    size_t m_nAllocations; ///< The number of allocations for this node.
    unsigned int m_nTreeLevel; ///< How deep is located this node in the tree?
    size_t m_nWeight; ///< Weight of this node expressed as WEIGHT_MULTIPLIER*(m_nBytes/TOTAL_TREE_BYTES)
    std::array<char, MAX_SITENAME_LEN> m_siteName; ///< Site name, NUL terminated
    std::array<MallocTreeNode_s*, MAX_CHILDREN_PER_NODE> m_pChildren; ///< Children nodes.
    unsigned int m_nChildrens;
    MallocTreeNode_s* m_pParent;

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

    void collect_json_stats_recursively(std::string& out)
    {
        // each node is a JSON object
        out += "\"" + std::string(&m_siteName[0], strlen(&m_siteName[0])) + "\":{";
        out += "\"nBytes\": " + std::to_string(m_nBytes) + ",";
        out += "\"nBytesDirect\": " + std::to_string(m_nBytesDirect) + ",";
        out += "\"nWeightPercentage\": " + std::to_string(get_weight_percentage()) + ",";
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
    bool m_bLastPushWasSuccessful = false;
    unsigned int m_nTreeNodesInUse = 0;
    unsigned int m_nMaxTreeDepth = 0;

    void init(void* caller) // triggers some MEMORY ALLOCATION
    {
        assert(m_pNodePool == nullptr);

        // initialize the memory pool of tree nodes
        m_pNodePool = fmpool_create(MallocTreeNode_t, MAX_TREE_NODES);

        // init the "current node" pointer to have the same name of the
        m_pRootNode = fmpool_get(MallocTreeNode_t, m_pNodePool);
        assert(m_pRootNode);
        m_nTreeNodesInUse++;
        m_pRootNode->init(NULL); // this is the tree root node
        // m_pRootNode->set_sitename_to_shlib_name_from_func_pointer(caller);
        m_pRootNode->set_sitename_to_threadname();
        m_nMaxTreeDepth = 0;

        m_pCurrentNode = m_pRootNode;
    }

    bool is_ready() { return m_pNodePool != NULL && m_pRootNode != NULL && m_pCurrentNode != NULL; }

    void push_new_node(const char* name)
    {
        if (m_pCurrentNode->m_nTreeLevel == MAX_TREE_LEVELS) {
            m_bLastPushWasSuccessful = false;
            return;
        }

        MallocTreeNode_t* n = m_pCurrentNode->get_child_by_name(name);
        if (n) {
            // this branch of the tree already exists, just move the cursor:
            m_pCurrentNode = n;
            m_bLastPushWasSuccessful = true;
        } else {
            // this branch needs to be created:
            n = fmpool_get(MallocTreeNode_t, m_pNodePool);
            if (n) {
                m_nTreeNodesInUse++;
                n->init(m_pCurrentNode);
                n->set_sitename(name);
                if (!m_pCurrentNode->link_new_children(n)) {
                    // release node back to the pool
                    fmpool_free(MallocTreeNode_t, n, m_pNodePool);
                    return;
                }

                // new node ready, move the cursor:
                m_pCurrentNode = n;
                m_bLastPushWasSuccessful = true;
                m_nMaxTreeDepth = std::max(m_nMaxTreeDepth, m_pCurrentNode->m_nTreeLevel);
            }
            // else: memory pool is full... FIXME: how to notify this???
        }
    }

    void pop_last_node()
    {
        if (m_bLastPushWasSuccessful) {
            MallocTreeNode_t* n = m_pCurrentNode->m_pParent;
            if (n)
                m_pCurrentNode = n;
            // else: we are already at the tree root... cannot pop... this is a logical mistake... FIXME: assert?
        }
    }

    void collect_json_stats_recursively(std::string& out)
    {
        out += "{";
        out += "\"nMaxTreeDepth\": " + std::to_string(m_nMaxTreeDepth) + ",";
        out += "\"nTreeNodesInUse\": " + std::to_string(m_nTreeNodesInUse) + ",";
        m_pRootNode->collect_json_stats_recursively(out);
        out += "}";
    }

    void compute_bytes_totals_recursively()
    {

        // NOTE: order is important: first compute "bytes total" across the whole tree
        m_pRootNode->compute_bytes_totals_recursively();

        // then we can compute node weigth across the whole tree:
        m_pRootNode->compute_node_weights_recursively(m_pRootNode->m_nBytes);
    }

} MallocTree_t;

MallocTree_t g_perthread_tree; // FIXME: must be per-thread

void mtag_init_if_needed(void* caller)
{
    if (!g_perthread_tree.is_ready()) {
        g_malloc_hook_active = 0; // deactivate hooks during initialization
        g_perthread_tree.init(caller);
        g_malloc_hook_active = 1; // reactivate hooks
    }
}

//------------------------------------------------------------------------------
// MallocTagScope
//------------------------------------------------------------------------------

// advance the per-thread cursor inside the malloc tree by 1 more level, adding the "tag_name" level
MallocTagScope::MallocTagScope(const char* tag_name)
{
    if (g_perthread_tree.is_ready())
        g_perthread_tree.push_new_node(tag_name);
}

// pop by 1 level the current per-thread cursor
MallocTagScope::~MallocTagScope()
{
    if (g_perthread_tree.is_ready())
        g_perthread_tree.pop_last_node();
}

//------------------------------------------------------------------------------
// mtag hooks
//------------------------------------------------------------------------------

void* mtag_malloc_hook(size_t size, void* caller)
{
    void* result = __libc_malloc(size);

    mtag_init_if_needed(caller);
    if (result)
        g_perthread_tree.m_pCurrentNode->track_malloc(size);

    // do logging
    g_malloc_hook_active = 0; // deactivate hooks for logging
    // printf("MYMALLOCHOOK %zu\n", size);
    g_malloc_hook_active = 1; // reactivate hooks

    return result;
}

void mtag_free_hook(void* __ptr)
{
    __libc_free(__ptr);

    // do logging
    g_malloc_hook_active = 0; // deactivate hooks for logging
    // printf("MYFREEHOOK %p\n", __ptr);
    g_malloc_hook_active = 1; // reactivate hooks
}

std::string malloctag_collect_stats_as_json()
{
    g_malloc_hook_active = 0; // deactivate hooks for logging

    // reserve enough space inside the output string:
    std::string ret;
    ret.reserve(4096);

    // now traverse the tree collecting stats:
    g_perthread_tree.compute_bytes_totals_recursively();
    g_perthread_tree.collect_json_stats_recursively(ret);

    g_malloc_hook_active = 1; // reactivate hooks
    return ret;
}

bool malloctag_write_stats_as_json_file(const std::string& fullpath)
{
    bool bwritten = false;

    g_malloc_hook_active = 0; // deactivate hooks for logging

    std::string fpath = fullpath;
    if (fpath.empty() && getenv(MTAG_STATS_OUTPUT_JSON_ENV))
        fpath = std::string(getenv(MTAG_STATS_OUTPUT_JSON_ENV));
    std::ofstream json_stats(fpath);
    if (json_stats.is_open()) {
        json_stats << malloctag_collect_stats_as_json() << std::endl;
        bwritten = true;
        json_stats.close();
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
