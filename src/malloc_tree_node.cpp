/*
 * malloc_tree_node.cpp
 *
 * Author: fmontorsi
 * Created: Aug 2023
 * License: Apache license
 *
 */

#include "private/malloc_tree_node.h"
#include "private/output_utils.h"
#include <dlfcn.h>
#include <sys/prctl.h>

//------------------------------------------------------------------------------
// Global functions
//------------------------------------------------------------------------------

std::string MallocTagGlibcPrimitive2String(MallocTagGlibcPrimitive_e t)
{
    switch (t) {
    case MTAG_GLIBC_PRIMITIVE_MALLOC:
        return "malloc";
    case MTAG_GLIBC_PRIMITIVE_REALLOC:
        return "realloc";
    case MTAG_GLIBC_PRIMITIVE_CALLOC:
        return "calloc";
    case MTAG_GLIBC_PRIMITIVE_FREE:
        return "free";

    default:
        return "INVALID";
    }
}

//------------------------------------------------------------------------------
// MallocTreeNode
//------------------------------------------------------------------------------

void MallocTreeNode::set_scope_name_to_shlib_name_from_func_pointer(void* funcpointer)
{
    Dl_info address_info;
    if (dladdr(funcpointer, &address_info) == 0 || address_info.dli_fname == nullptr) {
        strncpy(&m_scopeName[0], "UnknownSharedLib", MTAG_MAX_SCOPENAME_LEN);
    } else
        strncpy(&m_scopeName[0], address_info.dli_fname, MTAG_MAX_SCOPENAME_LEN);

    // FIXME: should we free the pointers inside "address_info"??
}

void MallocTreeNode::set_scope_name_to_threadname()
{
    // get thread name:
    prctl(PR_GET_NAME, &m_scopeName[0], 0, 0);
}

void MallocTreeNode::set_scope_name(const char* sitename)
{
    strncpy(&m_scopeName[0], sitename, MTAG_MAX_SCOPENAME_LEN);
    m_scopeName[MTAG_MAX_SCOPENAME_LEN - 1] = '\0';
}

bool MallocTreeNode::link_new_children(MallocTreeNode* new_child)
{
    if (m_nChildrens < MTAG_MAX_CHILDREN_PER_NODE) {
        m_pChildren[m_nChildrens++] = new_child;
        return true;
    }
    return false;
}

MallocTreeNode* MallocTreeNode::get_child_by_name(const char* name) const
{
    size_t nchars = std::min(strlen(name), (size_t)MTAG_MAX_SCOPENAME_LEN);
    for (unsigned int i = 0; i < m_nChildrens; i++)
        if (strncmp(&m_pChildren[i]->m_scopeName[0], name, nchars) == 0)
            return m_pChildren[i];
    return NULL;
}

void MallocTreeNode::collect_stats_recursively_MAP(MallocTagStatMap_t& out, const std::string& parent_kpi_prefix)
{
    // use an underscore to flatten the "address" of this node inside the tree into a single string:
    std::string fullName;
    if (m_nTreeLevel == 0)
        fullName = parent_kpi_prefix + ":" + get_node_name();
    else
        fullName = parent_kpi_prefix + "." + get_node_name();

    // provide a programmer-friendly way to get stats out of a "flat dictionary":
    out[fullName + ".nBytesTotalAllocated"] = m_nBytesTotalAllocated;
    out[fullName + ".nBytesSelfAllocated"] = m_nBytesSelfAllocated;
    out[fullName + ".nBytesSelfFreed"] = m_nBytesSelfFreed;
    out[fullName + ".nTimesEnteredAndExited"] = m_nTimesEnteredAndExited;

    for (unsigned int i = 0; i < MTAG_GLIBC_PRIMITIVE_MAX; i++) {
        std::string kpiName = fullName + ".nCallsTo_" + MallocTagGlibcPrimitive2String((MallocTagGlibcPrimitive_e)i);
        out[kpiName] = m_nAllocationsSelf[i];
    }

    // recurse
    for (unsigned int i = 0; i < m_nChildrens; i++) {
        m_pChildren[i]->collect_stats_recursively_MAP(out, fullName);
    }
}

void MallocTreeNode::collect_stats_recursively_JSON(std::string& out)
{
    // each node is a JSON object
    JsonUtils::start_object(out, "scope_" + get_node_name());

    JsonUtils::append_field(out, "nBytesTotalAllocated", m_nBytesTotalAllocated);
    JsonUtils::append_field(out, "nBytesSelfAllocated", m_nBytesSelfAllocated);
    JsonUtils::append_field(out, "nBytesSelfFreed", m_nBytesSelfFreed);
    JsonUtils::append_field(out, "nTimesEnteredAndExited", m_nTimesEnteredAndExited);
    JsonUtils::append_field(out, "nWeightPercentage", get_total_weight_percentage_str());

    for (unsigned int i = 0; i < MTAG_GLIBC_PRIMITIVE_MAX; i++)
        JsonUtils::append_field(
            out, "nCallsTo_" + MallocTagGlibcPrimitive2String((MallocTagGlibcPrimitive_e)i), m_nAllocationsSelf[i]);

    {
        JsonUtils::start_object(out, "nestedScopes");
        for (unsigned int i = 0; i < m_nChildrens; i++) {
            m_pChildren[i]->collect_stats_recursively_JSON(out);
            if (i < m_nChildrens - 1)
                // there's another node to dump:
                out += ",";
        }
        JsonUtils::end_object(out); // close childrenNodes
    }
    JsonUtils::end_object(out); // close the whole node object
}

#define MINIMAL_BYTES_TOTAL_THRESHOLD 1024
#define MINIMAL_WEIGHT_PERC_THRESHOLD 1

void MallocTreeNode::collect_stats_recursively_HUMANFRIENDLY(std::string& out)
{
    std::string b((m_nTreeLevel)*2, ' ');
    std::string i((m_nTreeLevel + 1) * 2, ' ');

    float w = get_total_weight_percentage();

    // try not to be too verbose:
    out += b + "scope_" + get_node_name() + "\n";
    if (m_nBytesTotalAllocated >= MINIMAL_BYTES_TOTAL_THRESHOLD && w >= MINIMAL_WEIGHT_PERC_THRESHOLD) {
        out += i + "nBytesTotalAlloc/SelfNet=" + GraphVizUtils::pretty_print_bytes(m_nBytesTotalAllocated) + "/"
            + GraphVizUtils::pretty_print_bytes(get_net_self_bytes())
            + "\t[SelfAllocated/SelfFreed=" + GraphVizUtils::pretty_print_bytes(m_nBytesSelfAllocated) + "/"
            + GraphVizUtils::pretty_print_bytes(m_nBytesSelfFreed) + "]\n";
        out += i + "nTimesEnteredAndExited=" + std::to_string(m_nTimesEnteredAndExited) + "\n";
        out += i + "nBytesSelfAllocatedPerVisit="
            + GraphVizUtils::pretty_print_bytes(get_avg_self_bytes_alloc_per_visit()) + "\n";
        out += i + "nWeightPercentage=" + get_total_weight_percentage_str();
        if (w >= 70) {
            if (m_nBytesTotalAllocated != m_nBytesSelfAllocated)
                out += "\t\t\t<<<- hot path";
            else
                out += "\t\t\t<<<- hot leaf";
        }
        out += "\n";

        for (unsigned int i = 0; i < m_nChildrens; i++)
            m_pChildren[i]->collect_stats_recursively_HUMANFRIENDLY(out);
    } else {
        out += i + "[hidden: below threshold of " + std::to_string(MINIMAL_BYTES_TOTAL_THRESHOLD) + "bytes (total)]\n";
    }
}

void MallocTreeNode::collect_stats_recursively_GRAPHVIZDOT(std::string& out)
{
    std::string thisNodeName = get_node_name();

    // for each node provide an overall view of
    // - total memory usage accounted for this node (both in bytes and as percentage)
    // - self memory usage (both in bytes and as percentage)
    std::string info;
    if (m_nBytesSelfAllocated != m_nBytesTotalAllocated)
        info = "total_alloc=" + GraphVizUtils::pretty_print_bytes(m_nBytesTotalAllocated) + " ("
            + get_total_weight_percentage_str() + "%)\\nself_alloc="
            + GraphVizUtils::pretty_print_bytes(m_nBytesSelfAllocated) + " (" + get_self_weight_percentage_str() + "%)";
    else
        // shorten the label:
        info = "total_alloc=self_alloc=" + GraphVizUtils::pretty_print_bytes(m_nBytesTotalAllocated) + " ("
            + get_total_weight_percentage_str() + "%)";

    info += "\\nself_freed=" + GraphVizUtils::pretty_print_bytes(m_nBytesSelfFreed);
    info += "\\nvisited_times=" + std::to_string(m_nTimesEnteredAndExited);
    info += "\\nself_alloc_per_visit=" + GraphVizUtils::pretty_print_bytes(get_avg_self_bytes_alloc_per_visit());

    for (unsigned int i = 0; i < MTAG_GLIBC_PRIMITIVE_MAX; i++)
        if (m_nAllocationsSelf[i])
            info += "\\nnum_" + MallocTagGlibcPrimitive2String((MallocTagGlibcPrimitive_e)i)
                + "_self=" + std::to_string(m_nAllocationsSelf[i]);

    // write a description of this node:
    std::string thisNodeLabel, thisNodeShape;
    if (m_pParent == NULL) {
        // for root node, provide a more verbose label
        thisNodeLabel = "thread=" + thisNodeName + "\\nTID=" + std::to_string(m_nThreadID) + "\\n" + info;
        thisNodeShape = "box"; // to differentiate from all other nodes
    } else {
        thisNodeLabel = "scope=" + thisNodeName + "\\n" + info;
    }

    // calculate the fillcolor in a range from 0-9 based on the "self weight";
    // the idea is to provide a intuitive indication of the self contributions of each malloc scope:
    // the bigger / eye-catching nodes will be those where a lot of byte allocations have been recorded,
    // regardless of what happened inside their children
    float self_w = get_self_weight_percentage();
    std::string thisNodeFillColor, thisNodeFontSize = "14";
    if (self_w < 5) {
        thisNodeFillColor = "1";
        thisNodeFontSize = "9";
    } else if (self_w < 10) {
        thisNodeFillColor = "2";
        thisNodeFontSize = "10";
    } else if (self_w < 20) {
        thisNodeFillColor = "3";
        thisNodeFontSize = "12";
    } else if (self_w < 40) {
        thisNodeFillColor = "4";
        thisNodeFontSize = "14";
    } else if (self_w < 60) {
        thisNodeFillColor = "5";
        thisNodeFontSize = "16";
    } else if (self_w < 80) {
        thisNodeFillColor = "6";
        thisNodeFontSize = "18";
    } else {
        thisNodeFillColor = "7";
        thisNodeFontSize = "20";
    }

    // finally add this node to the graph:
    std::string per_thread_node_name = std::to_string(m_nThreadID) + "_" + thisNodeName;
    GraphVizUtils::append_node(
        out, per_thread_node_name, thisNodeLabel, thisNodeShape, thisNodeFillColor, thisNodeFontSize);

    // write all the connections between this node and its children:
    for (unsigned int i = 0; i < m_nChildrens; i++) {
        std::string child_per_thread_node_name = std::to_string(m_nThreadID) + "_" + m_pChildren[i]->get_node_name();
        GraphVizUtils::append_edge(out, per_thread_node_name, child_per_thread_node_name);
    }

    // now recurse into each children:
    for (unsigned int i = 0; i < m_nChildrens; i++)
        m_pChildren[i]->collect_stats_recursively_GRAPHVIZDOT(out);
}

size_t MallocTreeNode::compute_bytes_totals_recursively() // returns total bytes accumulated by this node
{
    // postorder traversal of a tree:
    // first of all, traverse all children subtrees:
    size_t accumulated_bytes = 0;
    for (unsigned int i = 0; i < m_nChildrens; i++)
        accumulated_bytes += m_pChildren[i]->compute_bytes_totals_recursively();

    // finally "visit" this node, updating the bytes count, using all children contributions:
    m_nBytesTotalAllocated = accumulated_bytes + m_nBytesSelfAllocated;
    return m_nBytesTotalAllocated;
}

void MallocTreeNode::compute_node_weights_recursively(size_t rootNodeTotalBytes)
{
    if (rootNodeTotalBytes == 0) {
        m_nWeightTotal = m_nWeightSelf = 0;
    } else {
        // compute weight of this node:
        m_nWeightTotal = MTAG_NODE_WEIGHT_MULTIPLIER * m_nBytesTotalAllocated / rootNodeTotalBytes;
        m_nWeightSelf = MTAG_NODE_WEIGHT_MULTIPLIER * m_nBytesSelfAllocated / rootNodeTotalBytes;
    }

    // recurse:
    for (unsigned int i = 0; i < m_nChildrens; i++)
        m_pChildren[i]->compute_node_weights_recursively(rootNodeTotalBytes);
}
