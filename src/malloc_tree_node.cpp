/*
 * malloc_tree_node.cpp
 *
 * Author: fmontorsi
 * Created: Aug 2023
 * License: Apache license
 *
 */

#include "private/malloc_tree_node.h"

void MallocTreeNode_s::set_sitename_to_shlib_name_from_func_pointer(void* funcpointer)
{
    Dl_info address_info;
    if (dladdr(funcpointer, &address_info) == 0 || address_info.dli_fname == nullptr) {
        strncpy(&m_siteName[0], "UnknownSharedLib", MTAG_MAX_SITENAME_LEN);
    } else
        strncpy(&m_siteName[0], address_info.dli_fname,
            std::min(strlen(address_info.dli_fname), (size_t)MTAG_MAX_SITENAME_LEN));

    // FIXME: should we free the pointers inside "address_info"??
}
void MallocTreeNode_s::set_sitename_to_threadname()
{
    // get thread name:
    prctl(PR_GET_NAME, &m_siteName[0], 0, 0);

    // however that could not be unique (it depends if the software makes use of prctl() or pthread_setname_np())
    // so let's append the thread-ID (TID)
    int offset = strlen(&m_siteName[0]);
    if (offset > MTAG_MAX_SITENAME_LEN - 6)
        offset = MTAG_MAX_SITENAME_LEN - 6; // truncate... 6 digits should be enough
    snprintf(&m_siteName[offset], 6, "%d", syscall(SYS_gettid));
}
void MallocTreeNode_s::set_sitename(const char* sitename)
{
    strncpy(&m_siteName[0], sitename, std::min(strlen(sitename), (size_t)MTAG_MAX_SITENAME_LEN));
}
bool MallocTreeNode_s::link_new_children(MallocTreeNode_s* new_child)
{
    if (m_nChildrens < MTAG_MAX_CHILDREN_PER_NODE) {
        m_pChildren[m_nChildrens++] = new_child;
        return true;
    }
    return false;
}

MallocTreeNode_s* MallocTreeNode_s::get_child_by_name(const char* name) const
{
    size_t nchars = std::min(strlen(name), (size_t)MTAG_MAX_SITENAME_LEN);
    for (unsigned int i = 0; i < m_nChildrens; i++)
        if (strncmp(&m_pChildren[i]->m_siteName[0], name, nchars) == 0)
            return m_pChildren[i];
    return NULL;
}

void MallocTreeNode_s::collect_json_stats_recursively(std::string& out)
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

void MallocTreeNode_s::collect_graphviz_dot_output(std::string& out)
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

size_t MallocTreeNode_s::compute_bytes_totals_recursively() // returns total bytes accumulated by this node
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

void MallocTreeNode_s::compute_node_weights_recursively(size_t rootNodeTotalBytes)
{
    // weighr is defined as
    m_nWeight = MTAG_NODE_WEIGHT_MULTIPLIER * m_nBytes / rootNodeTotalBytes;
    for (unsigned int i = 0; i < m_nChildrens; i++)
        m_pChildren[i]->compute_node_weights_recursively(rootNodeTotalBytes);
}
