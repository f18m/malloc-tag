/*
 * output_utils.h
 * This is a PRIVATE header. It should not be included by any application directly.
 * This is used only during the build of malloc-tag library itself.
 *
 * Author: fmontorsi
 * Created: Sept 2023
 * License: Apache license
 *
 */

#pragma once

//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------

#include <vector>

//------------------------------------------------------------------------------
// GraphVizUtils
//------------------------------------------------------------------------------

// see https://graphviz.org/doc/info/lang.html
class GraphVizUtils {
public:
    static void start_digraph(std::string& out, const std::string& digraph_name,
        const std::vector<std::string>& labels = std::vector<std::string>(), const std::string& colorscheme = "reds9")
    {
        out += "digraph " + digraph_name + " {\n";
        out += " node [colorscheme=" + colorscheme + " style=filled]\n"; // apply a colorscheme to all nodes

        if (!labels.empty()) {
            out += " labelloc=\"b\"\n label=\"";
            for (const auto& l : labels)
                out += l + "\\n";
            out += "\"\n";
        }
    }
    static void end_digraph(std::string& out, const std::vector<std::string>& labels = std::vector<std::string>())
    {
        if (!labels.empty()) {
            out += " labelloc=\"b\"\n label=\"";
            for (const auto& l : labels)
                out += l + "\\n";
            out += "\"\n";
        }
        out += "}\n";
    }

    static void start_subgraph(std::string& out, const std::string& digraph_name,
        const std::vector<std::string>& labels, const std::string& colorscheme = "reds9")
    {
        out += " subgraph cluster_" + digraph_name + " {\n";
        out += "  node [colorscheme=" + colorscheme + " style=filled]\n"; // apply a colorscheme to all nodes

        if (!labels.empty()) {
            out += "  labelloc=\"b\"\n  label=\"";
            for (const auto& l : labels)
                out += l + "\\n";
            out += "\"\n";
        }
    }
    static void end_subgraph(std::string& out) { out += " }\n"; }

    static void append_node(std::string& out, const std::string& nodeName, const std::vector<std::string>& labels,
        const std::string& shape = "", const std::string& fillcolor = "", const std::string& fontsize = "")
    {
        // use double quotes around the node name in case it contains Graphviz-invalid chars e.g. '/'
        out += "  \"" + nodeName + "\" [";

        if (!labels.empty()) {
            out += "label=\"";
            for (const auto& l : labels)
                out += l + "\\n";
            out += "\"\n";
        }

        if (!shape.empty())
            out += " shape=" + shape;
        if (!fillcolor.empty())
            out += " fillcolor=" + fillcolor;
        if (!fontsize.empty())
            out += " fontsize=" + fontsize;

        out += "]\n";
    }

    static void append_edge(std::string& out, const std::string& nodeName1, const std::string& nodeName2)
    {
        // use double quotes around the node name in case it contains Graphviz-invalid chars e.g. '/'
        out += "  \"" + nodeName1 + "\" -> \"" + nodeName2 + "\"\n";
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
// JsonUtils
//------------------------------------------------------------------------------

class JsonUtils {
public:
    static void start_document(std::string& out)
    {
        // start object
        out += "{";
    }
    static void end_document(std::string& out)
    {
        // start object
        out += "}";
    }

    static void start_object(std::string& out, const std::string& object_name)
    {
        // start object
        out += "\"" + object_name + "\": {";
    }
    static void end_object(std::string& out)
    {
        // end object
        out += "}\n";
    }

    static void append_field(
        std::string& out, const std::string& field_name, const std::string& field_value, bool is_last = false)
    {
        out += "\"" + field_name + "\": \"" + field_value + "\"";
        if (!is_last)
            out += ",\n";
    }
    static void append_field(std::string& out, const std::string& field_name, size_t field_value, bool is_last = false)
    {
        out += "\"" + field_name + "\": " + std::to_string(field_value);
        if (!is_last)
            out += ",\n";
    }
};
