#!/usr/bin/python3

# Author: fmontorsi
# Created: Oct 2023
# License: Apache license

import json
import os
import sys
import decimal
import graphviz   # pip3 install graphviz
from decimal import *
from malloc_tag.libs.mtag_graphviz_utils import *

# =======================================================================================================
# GLOBALs
# =======================================================================================================

SCOPE_PREFIX = "scope_"
g_num_nodes = 0


# =======================================================================================================
# MallocTagNode
# =======================================================================================================

class MallocTagNode:
    """
    This class represents a node in the JSON tree produced by the malloc-tag library.
    """

    def __init__(self, owner_tid, level=1):
        global g_num_nodes
        self.childrenNodes = {}  # dict indexed by "scope name"

        # allocate a new ID for this node
        self.id = g_num_nodes
        g_num_nodes += 1

        # a few info coming from the "caller"
        self.nLevel = level
        self.ownerTID = owner_tid

    def load_json(self, node_dict: dict, name: str):
        # this is the "scope name"
        self.name = name

        # weights
        self.nTotalWeightPercentage = 0 # this will be recomputed on the fly later
        self.nSelfWeightPercentage = 0 # this will be recomputed on the fly later

        # load self properties
        self.nBytesTotalAllocated = node_dict["nBytesTotalAllocated"]
        self.nBytesSelfAllocated = node_dict["nBytesSelfAllocated"]
        self.nBytesSelfFreed = node_dict["nBytesSelfFreed"]
        self.nTimesEnteredAndExited = node_dict["nTimesEnteredAndExited"]
        self.nCallsTo_malloc = node_dict["nCallsTo_malloc"]
        self.nCallsTo_realloc = node_dict["nCallsTo_realloc"]
        self.nCallsTo_calloc = node_dict["nCallsTo_calloc"]
        self.nCallsTo_free = node_dict["nCallsTo_free"]

        # recursive load
        for scope in node_dict["nestedScopes"].keys():
            assert scope.startswith(SCOPE_PREFIX)
            name = scope[len(SCOPE_PREFIX) :]

            # load the node recursively
            t = MallocTagNode(owner_tid=self.ownerTID, level=self.nLevel+1)
            t.load_json(node_dict["nestedScopes"][scope], name)
            assert name not in self.childrenNodes

            # store the node
            self.childrenNodes[name] = t

    def get_as_dict(self):
        d = {
            "nBytesTotalAllocated": self.nBytesTotalAllocated,
            "nBytesSelfAllocated": self.nBytesSelfAllocated,
            "nBytesSelfFreed": self.nBytesSelfFreed,
            "nTimesEnteredAndExited": self.nTimesEnteredAndExited,
            # FIXME: rename to nTotalWeightPercentage also in the JSON output
            "nWeightPercentage": Decimal(self.nTotalWeightPercentage), # see usage of DecimalEncoder later on
            "nCallsTo_malloc": self.nCallsTo_malloc,
            "nCallsTo_realloc": self.nCallsTo_realloc,
            "nCallsTo_calloc": self.nCallsTo_calloc,
            "nCallsTo_free": self.nCallsTo_free,
            "nestedScopes": {},
        }
        for n in self.childrenNodes:
            d["nestedScopes"][SCOPE_PREFIX + n] = self.childrenNodes[n].get_as_dict()
        return d

    def save_as_graphviz_dot(self, graph):

        thisNodeShape = "ellipse"
        thisNodeLabels = []
        if self.nLevel == 1:
            thisNodeLabels.append(f"thread={self.name}")
            thisNodeLabels.append(f"TID={self.ownerTID}")
            thisNodeShape = "box"
        else:
            thisNodeLabels.append(f"scope={self.name}")

        # for each node provide an overall view of
        # - total memory usage accounted for this node (both in bytes and as percentage)
        # - self memory usage (both in bytes and as percentage)
        if self.nBytesSelfAllocated != self.nBytesTotalAllocated:
            thisNodeLabels.append(f"total_alloc={GraphVizUtils.pretty_print_bytes(self.nBytesTotalAllocated)}")
            thisNodeLabels.append(f"self_alloc={GraphVizUtils.pretty_print_bytes(self.nBytesSelfAllocated)}")
        else:
            # shorten the label:
            thisNodeLabels.append(f"total_alloc=self_alloc={GraphVizUtils.pretty_print_bytes(self.nBytesTotalAllocated)}")
        
        thisNodeLabels.append(f"self_freed={GraphVizUtils.pretty_print_bytes(self.nBytesSelfFreed)}")
        thisNodeLabels.append(f"visited_times={self.nTimesEnteredAndExited}")

        n = self.get_avg_self_bytes_alloc_per_visit()
        thisNodeLabels.append(f"self_alloc_per_visit={GraphVizUtils.pretty_print_bytes(n)}")

        thisNodeLabels.append(f"nCallsTo_malloc={self.nCallsTo_malloc}")
        thisNodeLabels.append(f"nCallsTo_realloc={self.nCallsTo_realloc}")
        thisNodeLabels.append(f"nCallsTo_calloc={self.nCallsTo_calloc}")
        thisNodeLabels.append(f"nCallsTo_free={self.nCallsTo_free}")

        # Calculate the fillcolor in a range from 0-9 based on the "self weight"
        # The idea is to provide an intuitive indication of the self contributions of each malloc scope:
        # The bigger/eye-catching nodes will be those where a lot of byte allocations have been recorded,
        # regardless of what happened inside their children
        thisNodeFillColor = ""
        thisNodeFontSize = "14"

        if self.nSelfWeightPercentage < 5:
            thisNodeFillColor = "1"
            thisNodeFontSize = "9"
        elif self.nSelfWeightPercentage < 10:
            thisNodeFillColor = "2"
            thisNodeFontSize = "10"
        elif self.nSelfWeightPercentage < 20:
            thisNodeFillColor = "3"
            thisNodeFontSize = "12"
        elif self.nSelfWeightPercentage < 40:
            thisNodeFillColor = "4"
            thisNodeFontSize = "14"
        elif self.nSelfWeightPercentage < 60:
            thisNodeFillColor = "5"
            thisNodeFontSize = "16"
        elif self.nSelfWeightPercentage < 80:
            thisNodeFillColor = "6"
            thisNodeFontSize = "18"
        else:
            thisNodeFillColor = "7"
            thisNodeFontSize = "20"
        
        # create a name that is unique in the whole graphviz DOT document:
        thisNodeName = self.get_graphviz_node_name()

        # finally add this node:
        graph.node(name=thisNodeName, label='\n'.join(thisNodeLabels), shape=thisNodeShape, fillcolor=thisNodeFillColor, fontsize=thisNodeFontSize)
            
        # write all the connections between this node and its children:
        for c in self.childrenNodes:
            edge_label = f"w={self.childrenNodes[c].nTotalWeightPercentage}%"
            graph.edge(thisNodeName, self.childrenNodes[c].get_graphviz_node_name(), label=edge_label);

        # now recurse into each children:
        for c in self.childrenNodes:
            self.childrenNodes[c].save_as_graphviz_dot(graph)

    def aggregate_with(self, other: "MallocTagNode"):
        # reset weights... they will need to be recomputed:
        self.nTotalWeightPercentage = 0
        self.nSelfWeightPercentage = 0

        # sum all "summable" properties
        self.nBytesTotalAllocated += other.nBytesTotalAllocated
        self.nBytesSelfAllocated += other.nBytesSelfAllocated
        self.nBytesSelfFreed += other.nBytesSelfFreed
        self.nTimesEnteredAndExited += other.nTimesEnteredAndExited
        self.nCallsTo_malloc += other.nCallsTo_malloc
        self.nCallsTo_realloc += other.nCallsTo_realloc
        self.nCallsTo_calloc += other.nCallsTo_calloc
        self.nCallsTo_free += other.nCallsTo_free

        # recurse into children:
        for scopeName in other.childrenNodes.keys():
            if scopeName in self.childrenNodes:
                # same scope is already present... aggregate!
                self.childrenNodes[scopeName].aggregate_with(
                    other.childrenNodes[scopeName]
                )
            else:
                # this is a new scope... present only in the 'other' node... add it
                self.childrenNodes[scopeName] = other.childrenNodes[scopeName]



    def get_num_levels(self):
        tot = 1
        if self.childrenNodes:
            # take the max from children nodes:
            tot += max(
                [self.childrenNodes[n].get_num_levels() for n in self.childrenNodes]
            )
        return tot

    def get_num_nodes(self):
        tot = 1  # this current node
        # recurse into children nodes:
        for t in self.childrenNodes.keys():
            tot += self.childrenNodes[t].get_num_nodes()
        return tot

    def get_avg_self_bytes_alloc_per_visit(self):
        if self.nTimesEnteredAndExited > 0:
            # it's questionable if we should instead use:
            #                get_net_self_bytes() / m_nTimesEnteredAndExited
            return self.nBytesSelfAllocated / self.nTimesEnteredAndExited
        return 0

    def get_graphviz_node_name(self):
        # create a name that is unique in the whole graphviz DOT document:
        return f"{self.ownerTID}_{self.name}"


    def collect_allocated_freed_recursively(self):
        # Postorder traversal of a tree:
        # First, traverse all children subtrees:
        alloc_bytes = 0
        freed_bytes = 0
        for child in self.childrenNodes:
            a, f = self.childrenNodes[child].collect_allocated_freed_recursively()
            alloc_bytes += a
            freed_bytes += f

        # Finally, "visit" this node, updating the bytes count, using all children contributions:
        self.nBytesTotalAllocated = alloc_bytes + self.nBytesSelfAllocated
        self.nBytesTotalFreed = freed_bytes + self.nBytesSelfFreed

        # Assume the provided pointers had been initialized to zero at the start of the recursion
        return self.nBytesTotalAllocated, self.nBytesTotalFreed

    def compute_node_weights_recursively(self, allTreesTotalAllocatedBytes):
        if allTreesTotalAllocatedBytes == 0:
            self.nTotalWeightPercentage = self.nSelfWeightPercentage = 0
        else:
            # Compute weight of this node:
            self.nTotalWeightPercentage = round(float(100 * self.nBytesTotalAllocated) / allTreesTotalAllocatedBytes, 2)
            self.nWeightSelf = round(float(100 * self.nBytesSelfAllocated) / allTreesTotalAllocatedBytes, 2)

        # recurse:
        for child in self.childrenNodes:
            self.childrenNodes[child].compute_node_weights_recursively(allTreesTotalAllocatedBytes)







