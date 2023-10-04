#!/usr/bin/python3

# Author: fmontorsi
# Created: Oct 2023
# License: Apache license

# =======================================================================================================
# GraphVizUtils
# =======================================================================================================

class GraphVizUtils:
    """
    This class provides utilities for producing .DOT graphviz outputs
    """

    def __init__(self):
        pass

    @staticmethod
    def pretty_print_bytes(bytes):
        # NOTE: we convert to kilo/mega/giga (multiplier=1000) not to kibi/mebi/gibi (multiplier=1024) bytes !!!
        if bytes < 1000:
            return str(bytes) + "B"
        elif bytes < 1000000:
            return str(bytes // 1000) + "kB"
        elif bytes < 1000000000:
            return str(bytes // 1000000) + "MB"
        else:
            return str(bytes // 1000000000) + "GB"

