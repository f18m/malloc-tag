/*
 * malloc_tree.h
 * This is a PRIVATE header. It should not be included by any application directly.
 * This is used only during the build of malloc-tag library itself.
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

#include "malloc_tree_node.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <mutex>
#include <unistd.h>

//------------------------------------------------------------------------------
// MallocTree
// A tree composed by MallocTreeNode data structures, with push() and pop()
// methods garantueed to be malloc-free and O(1).
// Also provides accessor functions to traverse the tree recursively to get
// the memory profiler stats.
// This class is not thread-safe except for few methods so there should be one
// MallocTree for each thread existing in the target application.
//------------------------------------------------------------------------------
class MallocTree {
public:
    //------------------------------------------------------------------------------
    // Init API
    //------------------------------------------------------------------------------

    bool init(size_t max_tree_nodes, size_t max_tree_levels, bool is_main_thread); // triggers some MEMORY ALLOCATION
    bool init(MallocTree* main_thread_tree)
    {
        // all secondary threads will create trees identical to the main-thread tree
        // (this might change if I see the memory usage of malloctag is too high)
        return init(main_thread_tree->m_nMaxTreeNodes, main_thread_tree->m_nMaxTreeLevels, false /* is_main_thread */);
    }

    //------------------------------------------------------------------------------
    // Malloc "scope" manipulation API
    //------------------------------------------------------------------------------

    bool push_new_node(const char* name); // must be malloc-free; if false is returned do NOT invoke pop_last_node()
    void pop_last_node(); // must be malloc-free
    void track_alloc_in_current_scope(MallocTagGlibcPrimitive_e type, size_t nBytes)
    {
        m_pCurrentNode->track_alloc(type, nBytes);
    }
    void track_free_in_current_scope(MallocTagGlibcPrimitive_e type, size_t nBytes)
    {
        if (!m_pCurrentNode->track_free(type, nBytes))
            m_nFreeTrackingFailed++;
    }

    //------------------------------------------------------------------------------
    // Memory profiling APIs
    // These functions are thread-safe because it's generally another thread that
    // will be accessing these functions
    //------------------------------------------------------------------------------

    void collect_stats_recursively(std::string& out, MallocTagOutputFormat_e format, const std::string& output_options);
    void collect_stats_recursively_MAP(MallocTagStatMap_t& out);

    size_t get_total_allocated_bytes_tracked() const
    {
        // call this function only after collect_stats_recursively() API.
        // the result will be an approximated result: it will report the total memory accounted by this tree
        // at the time collect_stats_recursively() API was called the last time.
        return m_nVmSizeAtCreation + m_pRootNode->get_total_allocated_bytes();
    }

    //------------------------------------------------------------------------------
    // Getters
    //------------------------------------------------------------------------------

    bool is_ready() const { return m_pNodePool != NULL && m_pRootNode != NULL && m_pCurrentNode != NULL; }

    pid_t get_tid() const { return m_nThreadID; }

    size_t get_memory_usage_in_bytes() const
    {
        // we ignore other very compact fields used by a MallocTree... the mempool of nodes
        // is by far the biggest memory usage:
        if (m_pNodePool)
            return fmpool_mem_usage(MallocTreeNode, m_pNodePool);
        return 0;
    }

    size_t get_max_nodes() const { return m_nMaxTreeNodes; }
    size_t get_max_levels() const { return m_nMaxTreeLevels; }

private:
    fmpool_t(MallocTreeNode) * m_pNodePool = NULL;
    MallocTreeNode* m_pRootNode = NULL; // created by the init()
    MallocTreeNode* m_pCurrentNode = NULL; // pointer to the current malloc scope inside the tree
    pid_t m_nThreadID = 0; // thread ID of owner thread

    // this is the best estimation we can deliver for the virtual memory mmap()ped by
    //  * dynamic linker to launch the main thread (if this is the main tree)
    //  * pthread library to launch the secondary thread (if this is a secondary tree owned by a secondary thread)
    size_t m_nVmSizeAtCreation = 0; // in bytes

    // last push status:
    unsigned int m_nPushNodeFailures = 0;

    // tree status:
    unsigned int m_nTreeNodesInUse = 0;
    unsigned int m_nTreeLevels = 0;
    unsigned int m_nFreeTrackingFailed = 0;

    // tree limits:
    unsigned int m_nMaxTreeNodes = MTAG_DEFAULT_MAX_TREE_NODES;
    unsigned int m_nMaxTreeLevels = MTAG_DEFAULT_MAX_TREE_LEVELS;

    // to be locked every time the tree structure is read or modified (e.g. visiting nodes with recursive functions):
    std::mutex m_lockTreeStructure;
};
