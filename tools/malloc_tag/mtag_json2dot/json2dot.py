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
from malloc_tag.libs.mtag_node import *
from malloc_tag.libs.mtag_tree import *
from malloc_tag.libs.mtag_snapshot import *

# =======================================================================================================
# GLOBALs
# =======================================================================================================

THIS_SCRIPT_PYPI_PACKAGE = "malloctag-tools"

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
        help="The name of the output Graphviz file. The file type is auto-detected from file extension. Supported extensions include: .dot, .svg, .png, .jpeg",
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
        help="The JSON file containing the malloc-tag snapshot that must be rendered.",
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
    }

def json2dot_main():
    config = parse_command_line()

    # load the malloctag snapshot file:
    t = MallocTagSnapshot()
    t.load_json(config["input_json"])
    t.print_stats()

    # if requested, save the output:
    if config["output_file"]:
        if not t.save_graphviz(config["output_file"]):
            # exit with non-zero exit code; as per logging, save_graphviz() should have printed already
            sys.exit(2)


# =======================================================================================================
# MAIN
# =======================================================================================================

if __name__ == "__main__":
    json2dot_main()
