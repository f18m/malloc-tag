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
from mtag_node import *
from mtag_tree import *
from mtag_snapshot import *

# =======================================================================================================
# GLOBALs
# =======================================================================================================

THIS_SCRIPT_VERSION = "0.0.1"

# =======================================================================================================
# MAIN HELPERS
# =======================================================================================================


def parse_command_line():
    """Parses the command line and returns the configuration as dictionary object."""
    parser = argparse.ArgumentParser(
        description="Utility to render JSON snapshots produced by the malloc-tag library in Graphviz DOT language."
    )

    # Optional arguments
    # NOTE: we cannot add required=True to --output option otherwise it's impossible to invoke this tool with just --version
    parser.add_argument(
        "-o",
        "--output",
        help="The name of the output Graphviz DOT file with aggregated stats.",
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
        help="The JSON file to analyze. If '-' the JSON is read from stdin.",
        default=None,
    )

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

    return {
        "input_json": args.input,
        "output_file": args.output,
    }


# =======================================================================================================
# MAIN
# =======================================================================================================

if __name__ == "__main__":
    config = parse_command_line()

    # load the malloctag snapshot file:
    t = MallocTagSnapshot()
    t.load_json(config["input_json"])
    t.print_stats()

    # if requested, save the output:
    if config["output_file"]:
        t.save_graphviz_dot(config["output_file"])
