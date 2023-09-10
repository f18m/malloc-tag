/*
 * malloc_tree_node.cpp
 *
 * Author: fmontorsi
 * Created: Aug 2023
 * License: Apache license
 *
 */

#include "private/malloc_tree_node.h"

void MallocTreeNode::set_sitename_to_shlib_name_from_func_pointer(void* funcpointer)
{
    Dl_info address_info;
    if (dladdr(funcpointer, &address_info) == 0 || address_info.dli_fname == nullptr) {
        strncpy(&m_scopeName[0], "UnknownSharedLib", MTAG_MAX_SCOPENAME_LEN);
    } else
        strncpy(&m_scopeName[0], address_info.dli_fname,
            std::min(strlen(address_info.dli_fname), (size_t)MTAG_MAX_SCOPENAME_LEN));

    // FIXME: should we free the pointers inside "address_info"??
}

void MallocTreeNode::set_sitename_to_threadname()
{
    // get thread name:
    prctl(PR_GET_NAME, &m_scopeName[0], 0, 0);
}

void MallocTreeNode::set_sitename(const char* sitename)
{
    strncpy(&m_scopeName[0], sitename, std::min(strlen(sitename), (size_t)MTAG_MAX_SCOPENAME_LEN));
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

void MallocTreeNode::collect_json_stats_recursively(std::string& out)
{
    // each node is a JSON object
    out += "\"" + get_node_name() + "\":{";
    out += "\"nBytes\": " + std::to_string(m_nBytesTotal) + ",";
    out += "\"nBytesDirect\": " + std::to_string(m_nBytesSelf) + ",";
    out += "\"nWeightPercentage\": " + get_weight_percentage_str() + ",";
    out += "\"nAllocations\": " + std::to_string(m_nAllocationsSelf) + ",";
    out += "\"nestedScopes\": { ";
    for (unsigned int i = 0; i < m_nChildrens; i++) {
        m_pChildren[i]->collect_json_stats_recursively(out);
        if (i < m_nChildrens - 1)
            // there's another node to dump:
            out += ",";
    }
    out += "}}"; // close childrenNodes + the whole node object
}

void MallocTreeNode::collect_graphviz_dot_output_recursively(std::string& out)
{
    std::string thisNodeName = get_node_name();

    // for each node provide an overall view of
    // - total memory usage accounted for this node (both in bytes and as percentage)
    // - self memory usage (both in bytes and as percentage)
    std::string weight;
    if (m_nBytesTotal != m_nBytesSelf)
        weight = "total=" + GraphVizUtils::pretty_print_bytes(m_nBytesTotal) + " (" + get_weight_percentage_str()
            + "%)\\nself=" + GraphVizUtils::pretty_print_bytes(m_nBytesSelf) + " (" + get_weight_self_percentage_str()
            + "%)";
    else
        // shorten the label:
        weight = "total=self=" + GraphVizUtils::pretty_print_bytes(m_nBytesTotal) + " (" + get_weight_percentage_str()
            + "%)";

    weight += "\\nnum_alloc_self=" + std::to_string(m_nAllocationsSelf);

    // write a description of this node:
    std::string thisNodeLabel, thisNodeShape;
    if (m_pParent == NULL) {
        // for root node, provide a more verbose label
        thisNodeLabel = "thread=" + thisNodeName + "\\nTID=" + std::to_string(m_nThreadID) + "\\n" + weight;
        thisNodeShape = "box"; // to differentiate from all other nodes
    } else {
        thisNodeLabel = "scope=" + thisNodeName + "\\n" + weight;
    }

    // calculate the fillcolor in a range from 0-9 based on the "self" memory usage:
    // the idea is to provide a intuitive indication of the self contributions of each malloc scope:
    float self_w = get_weight_self_percentage();
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
        m_pChildren[i]->collect_graphviz_dot_output_recursively(out);
}

size_t MallocTreeNode::compute_bytes_totals_recursively() // returns total bytes accumulated by this node
{
    // postorder traversal of a tree:
    // first of all, traverse all children subtrees:
    size_t accumulated_bytes = 0;
    for (unsigned int i = 0; i < m_nChildrens; i++)
        accumulated_bytes += m_pChildren[i]->compute_bytes_totals_recursively();

    // finally "visit" this node, updating the bytes count, using all children contributions:
    m_nBytesTotal = accumulated_bytes + m_nBytesSelf;
    return m_nBytesTotal;
}

void MallocTreeNode::compute_node_weights_recursively(size_t rootNodeTotalBytes)
{
    // compute weight of this node:
    m_nWeightTotal = MTAG_NODE_WEIGHT_MULTIPLIER * m_nBytesTotal / rootNodeTotalBytes;
    m_nWeightSelf = MTAG_NODE_WEIGHT_MULTIPLIER * m_nBytesSelf / rootNodeTotalBytes;

    // recurse:
    for (unsigned int i = 0; i < m_nChildrens; i++)
        m_pChildren[i]->compute_node_weights_recursively(rootNodeTotalBytes);
}
