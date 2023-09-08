/*
 * malloc_tree.h
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
#include <dlfcn.h>
#include <fstream>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

//------------------------------------------------------------------------------
// MallocTree
// A tree composed by MallocTreeNode data structures, with push() and pop()
// methods garantueed to be malloc-free and O(1).
// Also provides accessor functions to traverse the tree recursively to get
// the memory profiler stats.
// This class is not thread-safe, so there should be one MallocTree for each
// thread existing in the target application.
//------------------------------------------------------------------------------
class MallocTree {
public:
    //------------------------------------------------------------------------------
    // Init API
    //------------------------------------------------------------------------------

    bool init(size_t max_tree_nodes, size_t max_tree_levels); // triggers some MEMORY ALLOCATION
    bool init(MallocTree* main_thread_tree)
    {
        // all secondary threads will create trees identical to the main-thread tree
        // (this might change if I see the memory usage of malloctag is too high)
        return init(main_thread_tree->m_nMaxTreeNodes, main_thread_tree->m_nMaxTreeLevels);
    }

    //------------------------------------------------------------------------------
    // Malloc "scope" manipulation API
    //------------------------------------------------------------------------------

    void push_new_node(const char* name); // must be malloc-free
    void pop_last_node(); // must be malloc-free
    void track_malloc_in_current_scope(size_t nBytes) { m_pCurrentNode->track_malloc(nBytes); }

    //------------------------------------------------------------------------------
    // Memory profiling APIs
    //------------------------------------------------------------------------------

    void collect_stats_recursively(std::string& out, MallocTagOutputFormat_e format);
    void compute_bytes_totals_recursively();

    //------------------------------------------------------------------------------
    // Getters
    //------------------------------------------------------------------------------

    bool is_ready() { return m_pNodePool != NULL && m_pRootNode != NULL && m_pCurrentNode != NULL; }

    size_t get_memory_usage_in_bytes() const
    {
        // we ignore other very compact fields used by a MallocTree... the mempool of nodes
        // is by far the biggest memory usage:
        return fmpool_mem_usage(MallocTreeNode, m_pNodePool);
    }

private:
    fmpool_t(MallocTreeNode) * m_pNodePool = NULL;
    MallocTreeNode* m_pRootNode = NULL;
    MallocTreeNode* m_pCurrentNode = NULL;

    // last push status:
    unsigned int m_nPushNodeFailures = 0;
    bool m_bLastPushWasSuccessful = false;

    // tree status:
    unsigned int m_nTreeNodesInUse = 0;
    unsigned int m_nTreeLevels = 0;

    // tree limits:
    unsigned int m_nMaxTreeNodes = MTAG_DEFAULT_MAX_TREE_NODES;
    unsigned int m_nMaxTreeLevels = MTAG_DEFAULT_MAX_TREE_LEVELS;
};
