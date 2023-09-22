# malloc-tag

A lighweight, intrusive memory profiler that allows to categorize memory allocation inside C/C++ projects, for Linux systems.

In short this project provides a small dynamic library (.so) containing a *malloc interposer* plus some basic **tagging facility** to **instrument** your code and add a tag / category to **each** memory allocation inside C/C++ projects. 
Malloc interposition (more on that later on) allows to intercept **any memory allocation** done using standard malloc()/new operators even if that happens from inside 3rd party libraries, C++ STL library, etc.

The malloc interposer provided in this project is garantueed to perform only O(1) counter updates and then simply use the original glibc malloc() implementation. In practice the per-malloc() overhead imposed by this library boils down to a few conditional jumps, a pointer deferencing and an integer sum.

In summary, this library enables **minimal-overhead, per-thread memory profiling**. It has been designed to be integrated as **"always on" profiler** vs being enabled only for debugging purposes. The target is to enable developers to answer questions like:

* Why your application is using so much memory?
* In which part of the code should you focus your attention to decrease the memory footprint of your application?
* Is there a thread/part of the code allocating abnormal amounts of memory? 
 

# High-Level Design Criteria

* close-to-zero overhead compared to original glibc malloc/free implementations to allow "always on" behavior
* zero memory allocations inside the malloc/free hooks themselves
* multi-threading friendly: no global mutexes (across all application's threads)
* fast memory accounting (happening for each malloc/free operation), slow reporting (this is expected to be a human-driven process for profiling purposes)
* C++ aware

# Technical Implementation

## Overview

A graphical overview of how malloc-tag works:

![overview_svg](docs/malloctag_overview.svg?raw=true "Malloc-tag implementation overview")

You may be wondering: how is it possible to "intercept" any malloc() call done from "Your application" and route them to "MallocTag library"?
The answer is "ELF interposition": if malloctag.so shared library is loaded by the dynamic linker BEFORE the glibc shared library is loaded,
then the process image of "Your application" will use the malloc() defined by malloctag instead of the same identical function signature available
inside the GNU libc.
Check the optimal page https://maskray.me/blog/2021-05-16-elf-interposition-and-bsymbolic for a very in-deep overview of ELF interposition. 
Focus on the following two statements while reading that page:

* If a dynamic symbol is defined by multiple components, they don't conflict.
* For a symbol lookup [...] the definition from the first component wins.


## Internals

To achieve the (high level design criterias)[#high-level-design-criteria] the following implementation choices have been made:

* each per-thread memory stat tree has a limited number of levels, pre-defined at build time
* each tree level has a "tag" or "category" which is a limited-size string (limit pre-defined at build time)
* max tree depth (i.e. number of nested nodes) is pre-defined at build time
* per-thread enable/disable flag
* per-thread tree of malloc categories
* per-thread mutex to synchronize the "collector thread" (i.e. the thread using the main "malloc_collect_stats" API) and all other application threads


# Example output

malloc-tag profiler can produce output in a machine-friendly JSON format, see e.g. [minimal example JSON output](examples/minimal/minimal_stats.json) or in SVG format thanks to [Graphviz DOT utility](https://graphviz.org/), see e.g. [minimal example SVG output](examples/minimal/minimal_stats.dot.svg):

![minimal_example_svg](examples/minimal/minimal_stats.dot.svg?raw=true "Malloc-tag output for MINIMAL example")

From the picture above it becomes obvious that to improve/reduce memory usage, all the allocations accounted against the "minimal" scope and **all the code executing in the malloc scope "FuncC" should be improved**, since they have the **highest self-memory allocation**, as emphasized by the darkest red shade.

Profiling a more complex example, involving a simple application spawning 5 secondary pthreads, will produce such kind of graph:

![multithread_example_svg](examples/multithread/multithread_stats.dot.svg?raw=true "Malloc-tag output for MULTITHREAD example")

From this picture it should be evident that **all the memory allocations happen, regardless of the thread, in the malloc scope named "FuncB"** (look at the self memory usage of that node and also at the number of malloc operations!).


# How to use

## Part 1: instrumenting the code

1) build&install this project:

```
git clone https://github.com/f18m/malloc-tag.git
cd malloc-tag
make && make install
```

2) add `-lmalloc_tag` to your C/C++ project linker flags in order to link against malloc-tag library (see caveat about tcmalloc below)

3) add malloctag initialization as close as possible to the entrypoint of your application, e.g. as first instruction in your `main()`, using:

```
#include <malloc_tag.h>

int main() {
  MallocTagEngine::init();
  ...
}
```

4) whenever you want a snapshot of the memory profiling results to be written, invoke the API to write results on disk:

```
MallocTagEngine::write_stats();
```

This function might be placed at the very end of your main() or any other exit point. In alternative it can be hooked to a signal e.g. SIGUSR1 so that the
you will be able to write the statistics whenever you want at runtime.

5) optional: start by adding a few instances of MallocTagScope to "tag" the parts of your application which you believe are the most memory-hungry portions:

```
MallocTagScope nestedMallocScope("someInterestingPart");
```

## Part 2: run your application

After rebuilding your application, instrumented with malloc-tag, you can run your application as it runs normally.
It may be useful to add to the LD_LIBRARY_PATH env variable the directory where you dropped the "libmalloc_tag.so.1", if that's not a standard path considered by the [dynamic linker](https://man7.org/linux/man-pages/man8/ld.so.8.html).


## Part 3: analyze the results

Perhaps the easiest way to grasp the memory utilization of each "malloc tag" is to invoke the 'dot' utility, part of the [Graphviz package](https://graphviz.org/), to render the .dot stats file produced by the `MallocTagEngine::write_stats_on_disk()` API:

```
dot -Tsvg -O ${MTAG_STATS_OUTPUT_GRAPHVIZ_DOT}
```

Then open the resulting SVG file with any suitable viewer.


## TcMalloc integration

If your C/C++ project is using [tcmalloc](https://github.com/google/tcmalloc) that's fine.
malloc-tag has been tested together with tcmalloc with the caveat that the `-lmalloc_tag` library must be provided to the linker BEFORE the `-ltcmalloc` library is provided. 

As explained in the [Overview](#overview) section
this will work thanks to ELF interposition: the malloc() imeplementation of malloc-tag will be used and will use the `tcmalloc` malloc() to carry out the actual memory allocation.


# Environment variables

| Environment var                | Description                                                                                                                                       |
|--------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------|
| MTAG_STATS_OUTPUT_JSON         | The relative/full path to the output JSON file written by `MallocTagEngine::write_stats()`. If empty, no JSON output file will be produced. |
| MTAG_STATS_OUTPUT_GRAPHVIZ_DOT | The relative/full path to the output Graphviz DOT file written by `MallocTagEngine::write_stats()`. If empty, no DOT output file will be produced.                                                                                                                                                   |
| MTAG_SNAPSHOT_INTERVAL_SEC     | The time interval between two snapshots written by `MallocTagEngine::write_snapshot_if_needed()`. The special value zero means "disable snapshotting".   |
| MTAG_SNAPSHOT_OUTPUT_PREFIX_FILE_PATH | The filename prefix for snapshots written by `MallocTagEngine::write_snapshot_if_needed()`. If empty, no snapshot will be written.   |


# Links

## Useful references during malloc-tag development:

* Pixar TfMallocTag: https://openusd.org/dev/api/page_tf__malloc_tag.html
* GNU libc malloc hook alternative: https://stackoverflow.com/questions/17803456/an-alternative-for-the-deprecated-malloc-hook-functionality-of-glibc
* Replacing glibc malloc: https://www.gnu.org/software/libc/manual/html_node/Replacing-malloc.html
* Free-list Memory Pool in C: https://github.com/djoldman/fmpool

## About GNU libc malloc, tcmalloc, jemalloc:

* GNU libc malloc design: https://sourceware.org/glibc/wiki/MallocInternals
* GNU libc memory tunables: https://sourceware.org/glibc/wiki/MallocInternals
* libtcmalloc profiler: https://gperftools.github.io/gperftools/heapprofile.html
* jemalloc: https://jemalloc.net/

## About memory profiling in general under Linux:

* Memory flame graphs by Brendan Gregg: https://www.brendangregg.com/FlameGraphs/memoryflamegraphs.html
* History and status of dynamic tracing in Linux: https://www.joyfulbikeshedding.com/blog/2019-01-31-full-system-dynamic-tracing-on-linux-using-ebpf-and-bpftrace.html

# License

Apache 2.0 License


# TODO

* make max_tree_nodes, max_tree_levels configurable via env vars

# FUTURE

* explore bpftrace and generally-speaking eBPF-based approaches to memory observability
