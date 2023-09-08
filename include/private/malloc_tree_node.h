/*
 * malloc_tree_node.h
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

#define MTAG_MAX_SITENAME_LEN 20 // must be at least 16 bytes long due to use in prctl(PR_GET_NAME)
#define MTAG_MAX_CHILDREN_PER_NODE 16
#define MTAG_NODE_WEIGHT_MULTIPLIER 10000

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

//------------------------------------------------------------------------------
// MallocTreeNode_t
// A single node in the malloc tree structure.
// These nodes are created by the application using the MallocTagScope helper class
// in sensible parts of their code, to enable malloc tree profiling.
//
// MallocTreeNode_t nodes track both the memory they incur directly
// (m_nBytesDirect) but more importantly, the total memory allocated by
// themselves and any of their children (m_nBytes) and also its percentage weigth (m_nWeight).
//------------------------------------------------------------------------------

class MallocTreeNode {
private:
    size_t m_nBytes; ///< Allocated bytes by this or descendant nodes.
    size_t m_nBytesDirect; ///< Allocated bytes (only for this node).
    size_t m_nAllocations; ///< The number of allocations for this node.
    unsigned int m_nTreeLevel; ///< How deep is located this node in the tree?
    size_t m_nWeight; ///< Weight of this node expressed as MTAG_NODE_WEIGHT_MULTIPLIER*(m_nBytes/TOTAL_TREE_BYTES)
    std::array<char, MTAG_MAX_SITENAME_LEN> m_siteName; ///< Site name, NUL terminated
    std::array<MallocTreeNode*, MTAG_MAX_CHILDREN_PER_NODE> m_pChildren; ///< Children nodes.
    unsigned int m_nChildrens; ///< Number of valid children pointers in m_pChildren[]
    MallocTreeNode* m_pParent; ///< Pointer to parent node; NULL if this is the root node

public:
    void init(MallocTreeNode* parent)
    {
        m_nBytes = 0;
        m_nBytesDirect = 0;
        m_nAllocations = 0;
        m_nTreeLevel = parent ? parent->m_nTreeLevel + 1 : 0;
        m_siteName[0] = '\0';
        m_nChildrens = 0;
        m_pParent = parent;
    }

    //------------------------------------------------------------------------------
    // Node creation API
    //------------------------------------------------------------------------------

    void set_sitename_to_shlib_name_from_func_pointer(void* funcpointer);
    void set_sitename_to_threadname();
    void set_sitename(const char* sitename);
    bool link_new_children(MallocTreeNode* new_child);

    //------------------------------------------------------------------------------
    // Memory profiling APIs
    //------------------------------------------------------------------------------

    void track_malloc(size_t nBytes)
    {
        m_nBytesDirect += nBytes;
        m_nAllocations++;
    }

    void collect_json_stats_recursively(std::string& out);
    void collect_graphviz_dot_output(std::string& out);

    size_t compute_bytes_totals_recursively();
    void compute_node_weights_recursively(size_t rootNodeTotalBytes);

    //------------------------------------------------------------------------------
    // Getters
    //------------------------------------------------------------------------------

    MallocTreeNode* get_child_by_name(const char* name) const;

    unsigned int get_tree_level() const { return m_nTreeLevel; }
    MallocTreeNode* get_parent() { return m_pParent; }

    // IMPORTANT: total bytes will be zero unless compute_bytes_totals_recursively() has been invoked
    // previously on this tree node
    size_t get_total_bytes() const { return m_nBytes; }

    float get_weight_percentage() const
    {
        // to make this tree node more compact, we don't store a floating point directly;
        // rather we store a percentage in [0..1] range multiplied by MTAG_NODE_WEIGHT_MULTIPLIER
        return 100.0f * (float)m_nWeight / (float)MTAG_NODE_WEIGHT_MULTIPLIER;
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
};

// define a specialized type for the memory pool of MallocTreeNode_t
// Such kind of mempool allows us to AVOID memory allocation during the program execution,
// just during malloctag engine intialization
FMPOOL_INIT(MallocTreeNode)
