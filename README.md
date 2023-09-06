# malloc-tag
A small library to add a tag / category to each memory allocation inside C/C++ projects 


# High-Level Design Criteria

* close-to-zero overhead compared to original glibc malloc/free implementations to allow "always on" behavior
* zero memory allocations inside the malloc/free hooks themselves
* multi-threading friendly: no global mutexes (across all application's threads)
* fast memory accounting (happening for each malloc/free operation), slow reporting (this is expected to be a human-driven process for profiling purposes)
* C++ aware

# Technical Implementation

* categorization limited to a tree with limited number of levels, pre-defined at build time
* each tree level has a "tag" or "category" which is a limited-size string (limit pre-defined at build time)
* max tree depth (i.e. number of nested nodes) is pre-defined at build time
* per-thread enable/disable flag
* per-thread tree of malloc categories
* per-thread mutex to synchronize the "collector thread" (i.e. the thread using the main "malloc_collect_stats" API) and all other application threads

# Example output

malloc-tag profiler can produce output in a machine-friendly JSON format, see e.g. [minimal example JSON output](examples/minimal/minimal_stats.json) or in SVG format thanks to [Graphviz DOT utility](https://graphviz.org/), see e.g. [minimal example SVG output](examples/minimal/minimal_stats.dot.svg)

# Links

* Pixar TfMallocTag: https://openusd.org/dev/api/page_tf__malloc_tag.html
* GNU libc malloc hook alternative: https://stackoverflow.com/questions/17803456/an-alternative-for-the-deprecated-malloc-hook-functionality-of-glibc
* Free-list Memory Pool in C: https://github.com/djoldman/fmpool

# License

Apache 2.0 License

