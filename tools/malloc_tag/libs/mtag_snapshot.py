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
from malloc_tag.libs.mtag_tree import *

# =======================================================================================================
# GLOBALs
# =======================================================================================================

TREE_PREFIX = "tree_for_TID"

# =======================================================================================================
# DecimalEncoder
# =======================================================================================================

class DecimalEncoder(json.JSONEncoder):
    def default(self, obj):
        # 👇️ if passed in object is instance of Decimal
        # convert it to a string
        if isinstance(obj, Decimal):
            return round(float(obj), 2)
        # 👇️ otherwise use the default behavior
        return json.JSONEncoder.default(self, obj)

# =======================================================================================================
# MallocTagSnapshot
# =======================================================================================================

class MallocTagSnapshot:
    """
    This class represents a JSON snapshot produced by the malloc-tag library for an entire application.
    """

    def __init__(self):
        self.treeRegistry = {}  # dict indexed by TID

    def __expand(self, snapshot_dict: dict):
        self.pid = snapshot_dict["PID"]
        self.tmStartProfiling = snapshot_dict["tmStartProfiling"]
        self.tmCurrentSnapshot = snapshot_dict["tmCurrentSnapshot"]
        self.nBytesAllocBeforeInit = snapshot_dict["nBytesAllocBeforeInit"]
        self.nBytesMallocTagSelfUsage = snapshot_dict["nBytesMallocTagSelfUsage"]
        self.vmSizeNowBytes = snapshot_dict["vmSizeNowBytes"]
        self.vmRSSNowBytes = snapshot_dict["vmRSSNowBytes"]
        self.nTotalTrackedBytes = snapshot_dict["nTotalTrackedBytes"]

        for thread_tree in snapshot_dict.keys():
            if thread_tree.startswith("tree_for_"):
                t = MallocTree()
                t.load_json(snapshot_dict[thread_tree])
                assert t.tid not in self.treeRegistry
                self.treeRegistry[t.tid] = t

    def load_json(self, json_infile: str):
        # load the JSON
        wholejson = {}
        try:
            f = open(json_infile, "r")
            text = f.read()
            f.close()
            wholejson = json.loads(text)
        except json.decoder.JSONDecodeError as err:
            print("Invalid input JSON file '%s': %s" % (json_infile, err))
            sys.exit(1)

        # process it
        self.__expand(wholejson)

        # recompute weights
        self.compute_node_weights_recursively()

    def save_json(self, json_outfile: str):
        outdict = {
            "PID": self.pid,
            "tmStartProfiling": self.tmStartProfiling,
            "tmCurrentSnapshot": self.tmCurrentSnapshot,
        }
        for t in self.treeRegistry.keys():
            outdict[TREE_PREFIX + str(t)] = self.treeRegistry[t].get_as_dict()

        # add last few properties:
        outdict["nBytesAllocBeforeInit"] = self.nBytesAllocBeforeInit
        outdict["nBytesMallocTagSelfUsage"] = self.nBytesMallocTagSelfUsage
        outdict["vmSizeNowBytes"] = self.vmSizeNowBytes
        outdict["vmRSSNowBytes"] = self.vmRSSNowBytes
        outdict["nTotalTrackedBytes"] = self.nTotalTrackedBytes

        getcontext().prec = 2
        with open(json_outfile, "w", encoding="utf-8") as f:
            json.dump(outdict, f, ensure_ascii=False, indent=4, cls=DecimalEncoder)
        print(f"Saved postprocessed results into {json_outfile}.")

    def save_graphviz_dot(self, graphviz_dot_outfile: str):
        thegraph = graphviz.Digraph(comment='Malloc-tag snapshot')

        labels = []
        labels.append("Whole process stats")
        labels.append(
            "allocated_mem_before_malloctag_init=" + GraphVizUtils.pretty_print_bytes(self.nBytesAllocBeforeInit))
        labels.append(
            "allocated_mem_by_malloctag_itself=" + GraphVizUtils.pretty_print_bytes(self.nBytesMallocTagSelfUsage))
        labels.append("allocated_mem=" + GraphVizUtils.pretty_print_bytes(self.nTotalTrackedBytes))
        labels.append("vm_size_now=" + GraphVizUtils.pretty_print_bytes(self.vmSizeNowBytes))
        labels.append("vm_rss_now=" + GraphVizUtils.pretty_print_bytes(self.vmRSSNowBytes))
        labels.append("malloctag_start_ts=" + self.tmStartProfiling)
        labels.append("this_snapshot_ts=" + self.tmCurrentSnapshot)

        # create the main node:
        mainNodeName = f"Process_{self.pid}"
        thegraph.node(mainNodeName, label='\n'.join(labels))

        # used to compute weights later:
        totalloc, totfreed = self.collect_allocated_freed_recursively()

        # now create subgraphs for each and every tree: 
        for t in self.treeRegistry.keys():
            self.treeRegistry[t].save_as_graphviz_dot(thegraph)

            # compute the weight for the 't'-th tree:
            treealloc, treefree = self.treeRegistry[t].collect_allocated_freed_recursively()
            w = 0 if totalloc == 0 else 100*treealloc/totalloc
            wstr = f"%.2f%%" % w

            # add edge main node ---> tree
            thegraph.edge(mainNodeName, self.treeRegistry[t].get_graphviz_root_node_name(), label=wstr)


        #print(thegraph.source)
        thegraph.render(outfile=graphviz_dot_outfile)

    def print_stats(self):
        num_nodes = sum(
            [self.treeRegistry[t].get_num_nodes() for t in self.treeRegistry]
        )
        print(
            f"Loaded a total of {len(self.treeRegistry)} trees containing {num_nodes} nodes."
        )

    def aggregate_thread_trees(self, tid1: int, tid2: int):
        # do the aggregation
        self.treeRegistry[tid1].aggregate_with(self.treeRegistry[tid2])
        # remove the aggregated tree:
        del self.treeRegistry[tid2]
        # update all node weights:
        self.compute_node_weights_recursively()


    def collect_allocated_freed_recursively(self):
        totalloc = 0
        totfreed = 0
        for t in self.treeRegistry.keys():
            a, f = self.treeRegistry[t].collect_allocated_freed_recursively()
            totalloc += a
            totfreed += f
        return totalloc, totfreed

    def compute_node_weights_recursively(self):
        totalloc, totfreed = self.collect_allocated_freed_recursively()

        # in each tree, recompute node weights using the total allocated as denominator:
        for t in self.treeRegistry.keys():
            self.treeRegistry[t].compute_node_weights_recursively(totalloc)
