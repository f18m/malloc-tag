/*
    malloc-tag MINIMAL example

    This is a basic example showing how to
    * initialize the malloctag engine using MallocTagEngine::init()
    * create malloc scopes using MallocTagScope
    * produce memory allocation stats using MallocTagEngine::write_stats_on_disk()
*/

#include "malloc_tag.h"
#include <iostream>
#include <string.h>
#include <vector>

void FuncA();
void FuncB();
void FuncC();

void TopFunction()
{
    MallocTagScope noname("TopFunc"); // please account all mem allocs under the "TopFunc" name from this point onward

    FuncA();
    malloc(5); // allocation done directly by this TopFunction()
    FuncB();
    FuncC();
}

void FuncA()
{
    MallocTagScope noname("FuncA"); // please account all mem allocs under the "FuncA" name from this point onward

    malloc(100);
    FuncB();
}

void FuncB()
{
    MallocTagScope noname("FuncB"); // please account all mem allocs under the "FuncB" name from this point onward

    // use "new" to demonstrate that we also hook "new"
    new uint8_t[500];
}

void FuncC()
{
    MallocTagScope noname("FuncC"); // please account all mem allocs under the "FuncC" name from this point onward

    std::vector<uint8_t> dummyVec;
    dummyVec.reserve(1024);
}

int main()
{
    // as soon as main() is entered, start malloc tag engine:
    MallocTagEngine::init();

    // run some dummy memory allocations
    std::cout << "Hello world!" << std::endl;
    std::cout << "Starting some dumb allocations to exercise the malloc_tag library" << std::endl;
    TopFunction();
    std::cout << "VmRSS: " << MallocTagEngine::get_linux_rss_mem_usage_in_bytes() << "B" << std::endl;

    // dump stats in both JSON and graphviz formats
    if (MallocTagEngine::write_stats_on_disk(MTAG_OUTPUT_FORMAT_JSON))
        // output file is defined by env var MTAG_STATS_OUTPUT_JSON_ENV
        std::cout << "Wrote malloctag stats on file " << getenv(MTAG_STATS_OUTPUT_JSON_ENV) << std::endl;
    if (MallocTagEngine::write_stats_on_disk(MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT))
        // output file is defined by env var MTAG_STATS_OUTPUT_JSON_ENV
        std::cout << "Wrote malloctag stats on file " << getenv(MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV) << std::endl;

    std::cout << "Bye!" << std::endl;

    return 0;
}
