#!/usr/bin/python3

#
# This script takes as input a MallocTag JSON snapshot and will perform some postprocessing.
# Available postprocessing options right now are:
#  * tree aggregation based on the thread name regex
#
# Author: fmontorsi
# Created: Oct 2023
# License: Apache license
#

import argparse
import json
import os
import sys
import re

# =======================================================================================================
# GLOBALs
# =======================================================================================================

verbose = False
THIS_SCRIPT_VERSION = '0.0.1'
SCOPE_PREFIX = "scope_"
TREE_PREFIX = "tree_for_TID"

g_num_nodes = 0

# =======================================================================================================
# MallocTagNode
# =======================================================================================================

class MallocTagNode:
    """
        This class represents a node in the JSON tree produced by the malloc-tag library.
    """

    def __init__(self):
        global g_num_nodes
        self.childrenNodes = {} # dict indexed by "scope name"

        # allocate a new ID for this node
        self.id = g_num_nodes
        g_num_nodes += 1
        
    def load(self, node_dict: dict, name: str):
        # this is the "scope name"
        self.name = name
        #print(self.name)

        # load self properties
        self.nBytesTotalAllocated = node_dict["nBytesTotalAllocated"]
        self.nBytesSelfAllocated = node_dict["nBytesSelfAllocated"]
        self.nBytesSelfFreed = node_dict["nBytesSelfFreed"]
        self.nTimesEnteredAndExited = node_dict["nTimesEnteredAndExited"]
        self.nWeightPercentage = node_dict["nWeightPercentage"]
        self.nCallsTo_malloc = node_dict["nCallsTo_malloc"]
        self.nCallsTo_realloc = node_dict["nCallsTo_realloc"]
        self.nCallsTo_calloc = node_dict["nCallsTo_calloc"]
        self.nCallsTo_free = node_dict["nCallsTo_free"]

        # recursive load
        for scope in node_dict["nestedScopes"].keys():
            assert scope.startswith(SCOPE_PREFIX)
            name = scope[len(SCOPE_PREFIX):]

            # load the node recursively
            t = MallocTagNode()
            t.load(node_dict["nestedScopes"][scope], name)
            assert name not in self.childrenNodes

            # store the node
            self.childrenNodes[name] = t

    def get_as_dict(self):
        d = {
            'nBytesTotalAllocated': self.nBytesTotalAllocated,
            'nBytesSelfAllocated': self.nBytesSelfAllocated,
            'nBytesSelfFreed': self.nBytesSelfFreed,
            'nTimesEnteredAndExited': self.nTimesEnteredAndExited,
            'nWeightPercentage': self.nWeightPercentage,
            'nCallsTo_malloc': self.nCallsTo_malloc,
            'nCallsTo_realloc': self.nCallsTo_realloc,
            'nCallsTo_calloc': self.nCallsTo_calloc,
            'nCallsTo_free': self.nCallsTo_free,
            'nestedScopes': {}
        }
        for n in self.childrenNodes:
            d["nestedScopes"][SCOPE_PREFIX + n] = self.childrenNodes[n].get_as_dict()
        return d

    def get_num_levels(self):
        tot = 1 
        if self.childrenNodes:
            tot += max([self.childrenNodes[n].get_num_levels() for n in self.childrenNodes])
        return tot

    def get_num_nodes(self):
        tot = 1 # this current node
        for t in self.childrenNodes.keys():
            tot += self.childrenNodes[t].get_num_nodes()
        return tot

    def aggregate_with(self, other: 'MallocTagNode'):
        self.nBytesTotalAllocated += other.nBytesTotalAllocated
        self.nBytesSelfFreed += other.nBytesSelfFreed
        self.nTimesEnteredAndExited += other.nTimesEnteredAndExited
        self.nWeightPercentage += other.nWeightPercentage
        self.nCallsTo_malloc += other.nCallsTo_malloc
        self.nCallsTo_realloc += other.nCallsTo_realloc
        self.nCallsTo_calloc += other.nCallsTo_calloc
        self.nCallsTo_free += other.nCallsTo_free
        for scopeName in other.childrenNodes.keys():
            if scopeName in self.childrenNodes:
                # same scope is already present... aggregate!
                self.childrenNodes[scopeName].aggregate_with(other.childrenNodes[scopeName])
            else:
                # this is a new scope... present only in the 'other' node... add it
                self.childrenNodes[scopeName] = other.childrenNodes[scopeName]


# =======================================================================================================
# MallocTree
# =======================================================================================================

class MallocTree:
    """
        This class represents a JSON tree produced by the malloc-tag library for an entire thread.
    """

    def __init__(self):
        self.treeRootNode = None
        
    def load(self, tree_dict: dict):
        self.tid = tree_dict["TID"]
        self.name = tree_dict["ThreadName"]
        self.nPushNodeFailures = tree_dict["nPushNodeFailures"]
        self.nFreeTrackingFailed = tree_dict["nFreeTrackingFailed"]
        self.nMaxTreeNodes = tree_dict["nMaxTreeNodes"]
        self.nVmSizeAtCreation = tree_dict["nVmSizeAtCreation"]
        if self.nPushNodeFailures > 0:
            print(f"WARNING: found malloc-tag failures in tracking mem allocations inside the tree {self.name}")
        if self.nFreeTrackingFailed > 0:
            print(f"WARNING: found malloc-tag failures in tracking mem allocations inside the tree {self.name}")
        
        for k in tree_dict.keys():
            if k.startswith(SCOPE_PREFIX):
                # found the root node
                #print(k)
                assert self.treeRootNode is None
                self.treeRootNode = MallocTagNode()
                self.treeRootNode.load(tree_dict[k], k[len(SCOPE_PREFIX):])

    def get_as_dict(self):
        d = {
            'TID': self.tid,
            'ThreadName': self.name,
            "nTreeLevels": self.get_num_levels(),
            "nTreeNodesInUse": self.get_num_nodes(),
            "nMaxTreeNodes": self.nMaxTreeNodes,
            'nPushNodeFailures': self.nPushNodeFailures,
            'nFreeTrackingFailed': self.nFreeTrackingFailed,
            'nVmSizeAtCreation': self.nVmSizeAtCreation
        }
        d[SCOPE_PREFIX + self.treeRootNode.name] = self.treeRootNode.get_as_dict()
        return d

    def get_num_levels(self):
        # -1 is to get the level zero-based
        return self.treeRootNode.get_num_levels() - 1

    def get_num_nodes(self):
        return self.treeRootNode.get_num_nodes()

    def aggregate_with(self, other: 'MallocTree'):
        self.tid += other.tid
        self.name += "," + other.name
        self.nPushNodeFailures += other.nPushNodeFailures
        self.nFreeTrackingFailed += other.nFreeTrackingFailed
        self.treeRootNode.aggregate_with(other.treeRootNode)


# =======================================================================================================
# MallocTagSnapshot
# =======================================================================================================

#class MallocTagSnapshot_Encoder(json.JSONEncoder):
#    def default(self, obj):
#        if isinstance(obj, MallocTagSnapshot):
#            return 
#        # Let the base class default method raise the TypeError
#        return json.JSONEncoder.default(self, obj)

class MallocTagSnapshot:
    """
        This class represents a JSON snapshot produced by the malloc-tag library for an entire application.
    """

    def __init__(self):
        self.treeRegistry = {} # dict indexed by TID

    def load(self, json_infile: str):
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
        self.expand(wholejson)

    def expand(self, snapshot_dict: dict):
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
                t.load(snapshot_dict[thread_tree])
                assert t.tid not in self.treeRegistry
                self.treeRegistry[t.tid] = t

    def save(self, json_outfile: str):
        outdict = {
            'PID': self.pid,
            'tmStartProfiling': self.tmStartProfiling,
            'tmCurrentSnapshot': self.tmCurrentSnapshot
        }
        for t in self.treeRegistry.keys():
            outdict[TREE_PREFIX + str(t)] = self.treeRegistry[t].get_as_dict()
        
        # add last few properties:
        outdict["nBytesAllocBeforeInit"] = self.nBytesAllocBeforeInit
        outdict["nBytesMallocTagSelfUsage"] = self.nBytesMallocTagSelfUsage
        outdict["vmSizeNowBytes"] = self.vmSizeNowBytes
        outdict["vmRSSNowBytes"] = self.vmRSSNowBytes
        outdict["nTotalTrackedBytes"] = self.nTotalTrackedBytes

        with open(json_outfile, 'w', encoding='utf-8') as f:
            json.dump(outdict, f, ensure_ascii=False, indent=4)  ## , cls=MallocTagSnapshot_Encoder
        print(f"Saved postprocessed results into {json_outfile}.")

    def print_stats(self):
        num_nodes = sum([self.treeRegistry[t].get_num_nodes() for t in self.treeRegistry])
        print(f"Loaded a total of {len(self.treeRegistry)} trees containing {num_nodes} nodes.")

    def aggregate_thread_trees(self, tid1: int, tid2: int):
        # do the aggregation
        self.treeRegistry[tid1].aggregate_with(self.treeRegistry[tid2])
        # remove the aggregated tree:
        del self.treeRegistry[tid2]

# =======================================================================================================
# PostProcessConfig
# =======================================================================================================

class PostProcessAggregationRule:

    def __init__(self, ruleIdx: int):
        self.ruleIdx = ruleIdx
        self.matching_prefix = ""
    def load(self, cfg_dict):
        self.matching_prefix = cfg_dict["aggregate_trees"]["matching_prefix"]
    def logprefix(self):
        return f"Rule#{self.ruleIdx}:"
    def apply(self, snapshot: MallocTagSnapshot):
        # TODO
        regex = re.compile(self.matching_prefix)

        matching_tids = [tid for tid in snapshot.treeRegistry if re.match(regex, snapshot.treeRegistry[tid].name)]
        print(f"{self.logprefix()} Found trees matching the prefix [{self.matching_prefix}] with TIDs: {matching_tids}")

        if len(matching_tids) == 0:
            print(f"{self.logprefix()} Could not find any tree matching the prefix [{self.matching_prefix}]")
        elif len(matching_tids) == 1:
            print(f"{self.logprefix()} Found only 1 tree matching the prefix [{self.matching_prefix}]. Nothing to aggregate.")
        else:
            firstTid = matching_tids[0]
            for otherTid in matching_tids[1:]:
                snapshot.aggregate_thread_trees(firstTid, otherTid)
            print(f"{self.logprefix()} Aggregation completed.")

class PostProcessConfig:
    """
        This class represents some aggregation rules provided by the user
    """    

    def __init__(self):
        self.rules = []

    def load(self, cfg_json):
        wholejson = {}
        try:
            f = open(cfg_json, "r")
            text = f.read()
            f.close()
            wholejson = json.loads(text)
        except json.decoder.JSONDecodeError as err:
            print(f"Invalid configuration JSON file '{cfg_json}': {err}")
            sys.exit(1)

        nrule = 0
        for rule in wholejson:
            if not rule.startswith("rule"):
                print(f"In configuration JSON file '{cfg_json}': all root-level objects should be rules starting with [rule] prefix. Found: {rule}")
                sys.exit(1)
            if len(wholejson[rule]) != 1:
                print(f"In configuration JSON file '{cfg_json}': in rule '{rule}': expected exactly 1 mode")
                sys.exit(1)

            mode = next(iter(wholejson[rule]))

            if mode == "aggregate_trees":
                t = PostProcessAggregationRule(nrule)
                t.load(wholejson[rule])
                self.rules.append(t)
                nrule += 1
            else:
                print(f"In configuration JSON file '{cfg_json}': in rule '{rule}': found unsupported mode '{mode}'")
                sys.exit(1)
        print(f"Loaded {len(self.rules)} postprocessing rules from config file '{cfg_json}'.")

    def apply(self, snapshot: MallocTagSnapshot):
        for r in self.rules:
            r.apply(snapshot)

# =======================================================================================================
# MAIN HELPERS
# =======================================================================================================

def parse_command_line():
    """Parses the command line and returns the configuration as dictionary object."""
    parser = argparse.ArgumentParser(
        description="Utility to post-process snapshots produced by the malloc-tag library."
    )

    # Optional arguments
    # NOTE: we cannot add required=True to --output option otherwise it's impossible to invoke this tool with just --version
    parser.add_argument("-o", "--output", help="The name of the output JSON file with aggregated stats.", default=None)
    parser.add_argument("-c", "--config", help="JSON file specifying the postprocessing configuration.", default=None)
    parser.add_argument("-v", "--verbose", help="Be verbose.", action="store_true", default=False)
    parser.add_argument("-V", "--version", help="Print version and exit", action="store_true", default=False)
    # NOTE: we use nargs='?' to make it possible to invoke this tool with just --version
    parser.add_argument("input", nargs="?", help="The JSON file to analyze. If '-' the JSON is read from stdin.", default=None)

    if "COLUMNS" not in os.environ:
        os.environ["COLUMNS"] = "120"  # avoid too many line wraps
    args = parser.parse_args()

    global verbose
    verbose = args.verbose

    if args.version:
        print(f"Version: {THIS_SCRIPT_VERSION}")
        sys.exit(0)

    if args.input is None:
        print("Please provide the input file to process as positional argument")
        parser.print_help()
        sys.exit(os.EX_USAGE)

    if args.input != "-" and not os.path.isabs(args.input):
        # take absolute path
        args.input = os.path.join(os.getcwd(), args.input)

    if args.output is not None and not os.path.isabs(args.output):
        # take absolute path
        args.output = os.path.join(os.getcwd(), args.output)

    return {"input_json": args.input, "output_file": args.output, "postprocess_config_file": args.config}


# =======================================================================================================
# MAIN
# =======================================================================================================

if __name__ == "__main__":
    config = parse_command_line()

    # load the malloctag snapshot file:
    t = MallocTagSnapshot()
    t.load(config["input_json"])
    t.print_stats()

    # load postprocessing cfg:
    r = PostProcessConfig()
    if config["postprocess_config_file"]:
        r.load(config["postprocess_config_file"])

    # apply cfg:
    r.apply(t)

    # if requested, save the output:
    if config["output_file"]:
        t.save(config["output_file"])

