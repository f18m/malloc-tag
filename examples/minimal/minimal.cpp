#include "malloc_tag.h"
#include <iostream>
#include <unistd.h> // for linux

void FuncA();
void FuncB();

void TopFunction()
{
    MallocTagScope noname("Top"); // call-site "Top"

    FuncA();
    malloc(5); // allocation done directly by this TopFunction()
    FuncB();
}

void FuncA()
{
    MallocTagScope noname("A"); // call-site "A"

    malloc(100);
    FuncB();
}

void FuncB()
{
    MallocTagScope noname("B"); // call-site "B"

    // malloc(100);
    new uint8_t[200];
}

int main()
{
    std::cout << "Hello world! Starting some dumb allocations to exercise the malloc_tag library" << std::endl;

    TopFunction();
    // std::cout << malloc_collect_stats() << std::endl;
    if (malloctag_write_stats_on_disk(
            MTAG_OUTPUT_FORMAT_JSON)) // output file is defined by env var MTAG_STATS_OUTPUT_JSON_ENV
        std::cout << "Wrote malloctag stats on file " << getenv(MTAG_STATS_OUTPUT_JSON_ENV) << std::endl;
    if (malloctag_write_stats_on_disk(
            MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT)) // output file is defined by env var MTAG_STATS_OUTPUT_JSON_ENV
        std::cout << "Wrote malloctag stats on file " << getenv(MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV) << std::endl;

    std::cout << "Bye!" << std::endl;

    return 0;
}
