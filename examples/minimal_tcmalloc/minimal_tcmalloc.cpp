/*
    malloc-tag MINIMAL example to test interactions with TCMALLOC
*/

#include "malloc_tag.h" // from malloc-tag
#include <gperftools/malloc_extension.h> // from gperftools (=tcmalloc)
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <vector>

int main()
{
    // as soon as main() is entered, start malloc tag engine:
    MallocTagEngine::init();

    // run some dummy memory allocations
    std::cout << "Hello world from PID " << getpid() << std::endl;
    std::cout << "Allocating memory through malloc()" << std::endl;

    {
        MallocTagScope noname("TopFunc");

        // if everything works fine this memory allocation will go through:
        //   - malloc-tag
        //   - tcmalloc
        // libraries (in this sequence). IOW if this application wants to use tcmalloc instead of glibc malloc,
        // and use tcmalloc together with malloc-tag, that should work.
        // for (unsigned int i = 0; i < 1000; i++)
        void* p = malloc(1023);

        MallocExtension::Ownership t = MallocExtension::instance()->GetOwnership(p);
        if (t == MallocExtension::kOwned)
            std::cout << "TcMalloc has been correctly used to carry out memory allocation" << std::endl;

        // FIXME: check that also malloc-tag received processed that malloc(1023)
    }

    // dump stats in both JSON and graphviz formats
    if (MallocTagEngine::write_stats_on_disk())
        std::cout << "Wrote malloctag stats on disk as " << getenv(MTAG_STATS_OUTPUT_JSON_ENV) << " and "
                  << getenv(MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV) << std::endl;

    // dump also tcmalloc stats:
    if (false) {
        char tmp[16384]; /* to get a full set of stats, a very large array must be passed! */
        MallocExtension::instance()->GetStats(tmp, sizeof(tmp) - 1);
        std::cout << "Tcmalloc statistics:" << std::endl;
        std::cout << tmp << std::endl;
        // std::cout << MallocExtension::instance()->generic.current_allocated_bytes
    }

    // decomment this is you want to have the time to look at this process still running with e.g. "top"
    // sleep(100000);

    std::cout << "Bye!" << std::endl;

    return 0;
}
