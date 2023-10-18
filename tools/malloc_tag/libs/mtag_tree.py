#!/usr/bin/python3

# Author: fmontorsi
# Created: Oct 2023
# License: Apache license

import json
import os
import sys
import decimal
import graphviz  # pip3 install graphviz
from decimal import *

from malloc_tag.libs.mtag_graphviz_utils import *
from malloc_tag.libs.mtag_node import *

# =======================================================================================================
# GLOBALs
# =======================================================================================================

SCOPE_PREFIX = "scope_"

# =======================================================================================================
# MallocTree
# =======================================================================================================


class MallocTree:
    """
    This class represents a JSON tree produced by the malloc-tag library for an entire thread.
    """

    def __init__(self):
        self.treeRootNode = None
        self.manipulatedByRule = None

    def load_json(self, tree_dict: dict):
        """
        Initialize this object from the given dictionary (obtained from a malloc-tag JSON snapshot)
        """
        self.tid = tree_dict["TID"]
        self.name = tree_dict["ThreadName"]
        self.nPushNodeFailures = tree_dict["nPushNodeFailures"]
        self.nFreeTrackingFailed = tree_dict["nFreeTrackingFailed"]
        self.nMaxTreeNodes = tree_dict["nMaxTreeNodes"]
        self.nVmSizeAtCreation = tree_dict["nVmSizeAtCreation"]
        if "manipulatedByRule" in tree_dict:
            self.manipulatedByRule = tree_dict["manipulatedByRule"]
        if self.nPushNodeFailures > 0:
            print(
                f"WARNING: found malloc-tag failures in tracking mem allocations inside the tree {self.name}"
            )
        if self.nFreeTrackingFailed > 0:
            print(
                f"WARNING: found malloc-tag failures in tracking mem allocations inside the tree {self.name}"
            )

        for k in tree_dict.keys():
            if k.startswith(SCOPE_PREFIX):
                # found the root node
                # print(k)
                assert self.treeRootNode is None
                self.treeRootNode = MallocTagNode(owner_tid=self.tid)
                self.treeRootNode.load_json(tree_dict[k], k[len(SCOPE_PREFIX) :])

    def get_as_dict(self):
        d = {
            "TID": self.tid,
            "ThreadName": self.name,
            "nTreeLevels": self.get_num_levels(),
            "nTreeNodesInUse": self.get_num_nodes(),
            "nMaxTreeNodes": self.nMaxTreeNodes,
            "nPushNodeFailures": self.nPushNodeFailures,
            "nFreeTrackingFailed": self.nFreeTrackingFailed,
            "nVmSizeAtCreation": self.nVmSizeAtCreation,
        }
        if self.manipulatedByRule:
            d["manipulatedByRule"] = self.manipulatedByRule
        d[SCOPE_PREFIX + self.treeRootNode.name] = self.treeRootNode.get_as_dict()
        return d

    def save_as_graphviz_dot(self, graph):
        # let's use the digraph/subgraph label to convey extra info about this MallocTree:
        nTreeNodesInUse = self.get_num_nodes()
        labels = []
        labels.append(f"TID={self.tid}")
        labels.append(f"nPushNodeFailures={self.nPushNodeFailures}")
        labels.append(f"nTreeNodesInUse/Max={nTreeNodesInUse}/{self.nMaxTreeNodes}")
        if self.manipulatedByRule:
            labels.append(f"manipulatedByRule={self.manipulatedByRule}")

        # create one graph for each MallocTree
        tree_graph = graphviz.Digraph(
            name=f"cluster_TID{self.tid}",
            node_attr={"colorscheme": "reds9", "style": "filled"},
        )
        tree_graph.attr(label="\\n".join(labels), labelloc="b", fontsize="20")
        self.treeRootNode.save_as_graphviz_dot(tree_graph)

        # finally add the graph into the "big one" as subgraph
        graph.subgraph(tree_graph)

    def aggregate_with(self, other: "MallocTree", rule: "AggregationRuleDescriptor"):
        # the TID associated with a tree that has been aggregated with another one, makes no sense anymore;
        # on the other hand we still need to assign a number "as unique as possible" because the MallocTagSnapshot
        # class is using the TID as (unique) key for each tree;
        # the trick done here is to use the rule index as fake TID assuming that within the same rule, only one
        # tree will contain the aggregation result
        self.tid = rule.index
        self.manipulatedByRule = rule.desc
        self.name += "," + other.name
        self.nPushNodeFailures += other.nPushNodeFailures
        self.nFreeTrackingFailed += other.nFreeTrackingFailed
        self.nMaxTreeNodes = max(self.nMaxTreeNodes, other.nMaxTreeNodes)
        self.nVmSizeAtCreation = max(self.nVmSizeAtCreation, other.nVmSizeAtCreation)
        self.treeRootNode.aggregate_with(other.treeRootNode)
        self.treeRootNode.name = rule.name

    def collect_allocated_and_freed_recursively(self):
        return self.treeRootNode.collect_allocated_and_freed_recursively()
    
    def get_net_tracked_bytes(self):
        return self.treeRootNode.get_net_tracked_bytes()

    def compute_node_weights_recursively(self, allTreesTotalAllocatedBytes):
        self.treeRootNode.compute_node_weights_recursively(allTreesTotalAllocatedBytes)

    def get_num_levels(self):
        return self.treeRootNode.get_num_levels()

    def get_num_nodes(self):
        return self.treeRootNode.get_num_nodes()

    def get_graphviz_root_node_name(self):
        return self.treeRootNode.get_graphviz_node_name()
