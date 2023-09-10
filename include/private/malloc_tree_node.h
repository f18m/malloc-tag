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
#include <vector>

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MTAG_MAX_SCOPENAME_LEN 20 // must be at least 16 bytes long due to use in prctl(PR_GET_NAME)
#define MTAG_MAX_CHILDREN_PER_NODE 16

// the use of MTAG_NODE_WEIGHT_MULTIPLIER is just a trick that allows to store a percentage (ranging 0-100)
// with 2 decimal digits of accuray (0.01-0.99) using an integer instead of a "float" or "double"
// (to save memory)
#define MTAG_NODE_WEIGHT_MULTIPLIER 10000

//------------------------------------------------------------------------------
// Utils
//------------------------------------------------------------------------------

// see https://graphviz.org/doc/info/lang.html
class GraphVizUtils {
public:
    static void start_digraph(std::string& out, const std::string& digraph_name, const std::vector<std::string>& labels,
        const std::string& colorscheme = "reds9")
    {
        out += "digraph " + digraph_name + " {\n";
        out += "node [colorscheme=" + colorscheme + " style=filled]\n"; // apply a colorscheme to all nodes

        if (!labels.empty()) {
            out += "labelloc=\"b\"\nlabel=\"";
            for (const auto& l : labels)
                out += l + "\\n";
            out += "\"\n";
        }
    }
    static void end_digraph(std::string& out) { out += "}\n"; }

    static void start_subgraph(std::string& out, const std::string& digraph_name,
        const std::vector<std::string>& labels, const std::string& colorscheme = "reds9")
    {
        out += "subgraph cluster_" + digraph_name + " {\n";
        out += "node [colorscheme=" + colorscheme + " style=filled]\n"; // apply a colorscheme to all nodes

        if (!labels.empty()) {
            out += "labelloc=\"b\"\nlabel=\"";
            for (const auto& l : labels)
                out += l + "\\n";
            out += "\"\n";
        }
    }

    static void append_node(std::string& out, const std::string& nodeName, const std::string& label,
        const std::string& shape = "", const std::string& fillcolor = "")
    {
        // use double quotes around the node name in case it contains Graphviz-invalid chars e.g. '/'
        out += "\"" + nodeName + "\" [label=\"" + label + "\"";

        if (!shape.empty())
            out += " shape=" + shape;
        if (!fillcolor.empty())
            out += " fillcolor=" + fillcolor;

        out += "]\n";
    }

    static void append_edge(std::string& out, const std::string& nodeName1, const std::string& nodeName2)
    {
        // use double quotes around the node name in case it contains Graphviz-invalid chars e.g. '/'
        out += "\"" + nodeName1 + "\" -> \"" + nodeName2 + "\"\n";
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
// (m_nBytesSelf) but more importantly, the total memory allocated by
// themselves and any of their children (m_nBytes) and also its percentage weigth (m_nWeight).
//------------------------------------------------------------------------------

class MallocTreeNode {
public:
    //------------------------------------------------------------------------------
    // Node creation API
    //------------------------------------------------------------------------------

    void init(MallocTreeNode* parent, pid_t threadID)
    {
        m_nBytesTotal = 0;
        m_nBytesSelf = 0;
        m_nAllocationsSelf = 0;
        m_nWeightTotal = 0;
        m_nWeightSelf = 0;
        m_nTreeLevel = parent ? parent->m_nTreeLevel + 1 : 0;
        m_nThreadID = threadID;
        m_scopeName[0] = '\0';
        m_nChildrens = 0;
        m_pParent = parent;
    }

    void set_sitename_to_shlib_name_from_func_pointer(void* funcpointer);
    void set_sitename_to_threadname();
    void set_sitename(const char* sitename);
    bool link_new_children(MallocTreeNode* new_child);

    //------------------------------------------------------------------------------
    // Memory profiling APIs
    //------------------------------------------------------------------------------

    void track_malloc(size_t nBytes)
    {
        m_nBytesSelf += nBytes;
        m_nAllocationsSelf++;
    }

    void collect_json_stats_recursively(std::string& out);
    void collect_graphviz_dot_output_recursively(std::string& out);

    size_t compute_bytes_totals_recursively();
    void compute_node_weights_recursively(size_t rootNodeTotalBytes);

    //------------------------------------------------------------------------------
    // Getters
    //------------------------------------------------------------------------------

    MallocTreeNode* get_child_by_name(const char* name) const;

    unsigned int get_tree_level() const { return m_nTreeLevel; }
    MallocTreeNode* get_parent() { return m_pParent; }
    pid_t get_tid() const { return m_nThreadID; }

    // IMPORTANT: total bytes will be zero unless compute_bytes_totals_recursively() has been invoked
    // previously on this tree node
    size_t get_total_bytes() const { return m_nBytesTotal; }

    float get_weight_percentage() const
    {
        // to make this tree node more compact, we don't store a floating point directly;
        // rather we store a percentage in [0..1] range multiplied by MTAG_NODE_WEIGHT_MULTIPLIER
        return 100.0f * (float)m_nWeightTotal / (float)MTAG_NODE_WEIGHT_MULTIPLIER;
    }
    float get_weight_self_percentage() const
    {
        // to make this tree node more compact, we don't store a floating point directly;
        // rather we store a percentage in [0..1] range multiplied by MTAG_NODE_WEIGHT_MULTIPLIER
        return 100.0f * (float)m_nWeightSelf / (float)MTAG_NODE_WEIGHT_MULTIPLIER;
    }

    std::string get_weight_percentage_str() const
    {
        char ret[16];
        // ensure only 2 digits of accuracy:
        snprintf(ret, 15, "%.2f", get_weight_percentage());
        return std::string(ret);
    }
    std::string get_weight_self_percentage_str() const
    {
        char ret[16];
        // ensure only 2 digits of accuracy:
        snprintf(ret, 15, "%.2f", get_weight_self_percentage());
        return std::string(ret);
    }

    std::string get_node_name() const
    {
        // to make this tree node more compact, we don't store a std::string which might
        // trigger unwanted memory allocations (to resize itself), rather we store a
        // fixed-size array of chars:
        return std::string(&m_scopeName[0], strlen(&m_scopeName[0]));
    }

private:
    size_t m_nBytesTotal; // Allocated bytes by this node and ALL its descendant nodes. Computed at "stats collection
                          // time".
    size_t m_nBytesSelf; // Allocated bytes only for THIS node.
    size_t m_nAllocationsSelf; // The number of allocations for this node.
    unsigned int m_nTreeLevel; // How deep is located this node in the tree?
    size_t m_nWeightTotal; // Weight of this node expressed as MTAG_NODE_WEIGHT_MULTIPLIER*(m_nBytes/TOTAL_TREE_BYTES)
    size_t m_nWeightSelf; // Weight of this node expressed as MTAG_NODE_WEIGHT_MULTIPLIER*(m_nBytes/TOTAL_TREE_BYTES)
    pid_t m_nThreadID; // ID of the thread where the allocations will take place
    std::array<char, MTAG_MAX_SCOPENAME_LEN>
        m_scopeName; // Memory allocation scope name, NUL terminated. Defined via use of MallocTagScope.
    std::array<MallocTreeNode*, MTAG_MAX_CHILDREN_PER_NODE> m_pChildren; // Children nodes.
    unsigned int m_nChildrens; // Number of valid children pointers in m_pChildren[]
    MallocTreeNode* m_pParent; // Pointer to parent node; NULL if this is the root node
};

// define a specialized type for the memory pool of MallocTreeNode_t
// Such kind of mempool allows us to AVOID memory allocation during the program execution,
// just during malloctag engine intialization
FMPOOL_INIT(MallocTreeNode)
