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
import decimal
import importlib
from decimal import *
from malloc_tag.libs.mtag_node import *
from malloc_tag.libs.mtag_tree import *
from malloc_tag.libs.mtag_snapshot import *

# =======================================================================================================
# GLOBALs
# =======================================================================================================

THIS_SCRIPT_PYPI_PACKAGE = "malloctag-tools"


# =======================================================================================================
# PostProcessAggregationRule
# =======================================================================================================

class PostProcessAggregationRule:

    RULE_NAME = "aggregate_trees"
    PROPERTY_NAME = "matching_regex"

    def __init__(self, ruleIdx: int):
        self.ruleIdx = ruleIdx
        self.matchingRegex = ""
        self.regex = None

    def load(self, cfg_dict):

        rulename = PostProcessAggregationRule.RULE_NAME
        pname = PostProcessAggregationRule.PROPERTY_NAME

        if rulename not in cfg_dict:
            print(f"{self.logprefix()} Invalid syntax for aggregation rule.")
            return False

        if pname not in cfg_dict[rulename]:
            print(f"{self.logprefix()} Missing property {pname} inside {rulename}.")
            return False

        self.matchingRegex = cfg_dict[rulename][pname]
        try:
            self.regex = re.compile(self.matchingRegex)
            return True
        except:
            print(
                f"{self.logprefix()} Invalid regex [{self.matchingRegex}]. Aborting."
            )
            return False

    def logprefix(self):
        return f"Rule#{self.ruleIdx}:"

    def apply(self, snapshot: MallocTagSnapshot):
        matching_tids = [
            tid
            for tid in snapshot.treeRegistry
            if re.match(self.regex, snapshot.treeRegistry[tid].name)
        ]
        if len(matching_tids) == 0:
            print(
                f"{self.logprefix()} Could not find any tree matching the regex [{self.matchingRegex}]"
            )
        elif len(matching_tids) == 1:
            print(
                f"{self.logprefix()} Found only 1 tree matching the regex [{self.matchingRegex}]. Nothing to aggregate."
            )
        else:
            print(
                f"{self.logprefix()} Found {len(matching_tids)} trees matching the prefix [{self.matchingRegex}] with TIDs: {matching_tids}"
            )

            rule = AggregationRuleDescriptor(self.ruleIdx, self.matchingRegex, f"{self.logprefix()} aggregate threads {self.matchingRegex}")

            firstTid = matching_tids[0]
            for otherTid in matching_tids[1:]:
                snapshot.aggregate_thread_trees(firstTid, otherTid, rule)
            print(f"{self.logprefix()} Aggregation completed.")


# =======================================================================================================
# PostProcessConfig
# =======================================================================================================

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
                print(
                    f"WARN: In configuration JSON file '{cfg_json}': ignoring key not starting with [rule] prefix: '{rule}'"
                )
                continue
            if len(wholejson[rule]) != 1:
                print(
                    f"In configuration JSON file '{cfg_json}': in rule '{rule}': expected exactly 1 mode"
                )
                sys.exit(1)

            mode = next(iter(wholejson[rule]))

            if mode == PostProcessAggregationRule.RULE_NAME:
                t = PostProcessAggregationRule(nrule)
                if not t.load(wholejson[rule]):
                    sys.exit(1)
                self.rules.append(t)
                nrule += 1
            else:
                print(
                    f"In configuration JSON file '{cfg_json}': in rule '{rule}': found unsupported mode '{mode}'"
                )
                sys.exit(1)
        print(
            f"Loaded {len(self.rules)} postprocessing rules from config file '{cfg_json}'."
        )

    def apply(self, snapshot: MallocTagSnapshot):
        if len(self.rules) == 0:
            print(
                f"No postprocessing rules specified (see --config). The malloc-tag snapshot will not be manipulated."
            )
            return
        for r in self.rules:
            r.apply(snapshot)


# =======================================================================================================
# MAIN HELPERS
# =======================================================================================================

def parse_command_line():
    """Parses the command line and returns the configuration as dictionary object."""
    parser = argparse.ArgumentParser(
        description="Utility to post-process JSON snapshots produced by the malloc-tag library."
    )

    # Optional arguments
    # NOTE: we cannot add required=True to --output option otherwise it's impossible to invoke this tool with just --version
    parser.add_argument(
        "-o",
        "--output",
        help="The name of the output JSON file where post-processed results must be stored.",
        default=None,
    )
    parser.add_argument(
        "-c",
        "--config",
        help="JSON file specifying the postprocessing configuration.",
        default=None,
    )
    parser.add_argument(
        "-v", "--verbose", help="Be verbose.", action="store_true", default=False
    )
    parser.add_argument(
        "-V",
        "--version",
        help="Print version and exit",
        action="store_true",
        default=False,
    )
    # NOTE: we use nargs='?' to make it possible to invoke this tool with just --version
    parser.add_argument(
        "input",
        nargs="?",
        help="The malloc-tag snapshot JSON file to analyze.",
        default=None,
    )

    if "COLUMNS" not in os.environ:
        os.environ["COLUMNS"] = "120"  # avoid too many line wraps
    args = parser.parse_args()

    global verbose
    verbose = args.verbose

    if args.version:
        try:
            from importlib.metadata import version
        except:
            from importlib_metadata import version
        this_script_version = version(THIS_SCRIPT_PYPI_PACKAGE)
        print(f"Version: {this_script_version}")
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

    return {
        "input_json": args.input,
        "output_file": args.output,
        "postprocess_config_file": args.config,
    }

def postprocess_main():
    config = parse_command_line()

    # load the malloctag snapshot file:
    t = MallocTagSnapshot()
    t.load_json(config["input_json"])
    t.print_stats()

    # load postprocessing cfg:
    r = PostProcessConfig()
    if config["postprocess_config_file"]:
        r.load(config["postprocess_config_file"])

    # apply cfg:
    r.apply(t)

    # if requested, save the output:
    if config["output_file"]:
        if not t.save_json(config["output_file"]):
            # exit with non-zero exit code; as per logging, save_json() should have printed already
            sys.exit(2)


# =======================================================================================================
# MAIN
# =======================================================================================================

if __name__ == "__main__":
    postprocess_main()
