/*
    malloc-tag MINIMAL example to test interactions with TCMALLOC
*/

#include "malloc_tag.h" // from malloc-tag
#include <gperftools/malloc_extension.h> // from gperftools (=tcmalloc)
#include <iostream>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <vector>

#define MAIN_THREAD_NAME "minimal_tcm"
#define MALLOC_AMOUNT 567

int main()
{
    prctl(PR_SET_NAME, MAIN_THREAD_NAME); // shorten the name of this thread

    // as soon as main() is entered, start malloc tag engine:
    MallocTagEngine::init();

    // run some dummy memory allocations
    std::cout << "Hello world from PID " << getpid() << std::endl;

    {
        MallocTagScope noname("OuterScope");
        {
            MallocTagScope noname("InnerScope");

            // if everything works fine this memory allocation will go through:
            //   - malloc-tag
            //   - tcmalloc
            // libraries (in this sequence). IOW if this application wants to use tcmalloc instead of glibc malloc,
            // and use tcmalloc together with malloc-tag, that should work.
            std::cout << "Allocating " << MALLOC_AMOUNT << " bytes through malloc()" << std::endl;
            void* p = malloc(MALLOC_AMOUNT);

            MallocExtension::Ownership t = MallocExtension::instance()->GetOwnership(p);
            if (t == MallocExtension::kOwned)
                std::cout << "SUCCESS: TcMalloc has been used to carry out memory allocation" << std::endl;
            else
                std::cout
                    << "FAILURE: apparently the malloc() operation has been served by a non-tcmalloc implementation"
                    << std::endl;

            // check that also malloc-tag has processed that malloc() operation
            MallocTagStatMap_t mtag_stats = MallocTagEngine::collect_stats();
            // for (const auto& it : mtag_stats)
            //    std::cout << it.first << "=" << it.second << std::endl;

            std::string key_for_inner_scope = MallocTagEngine::get_stat_key_prefix_for_thread()
                + std::string(MAIN_THREAD_NAME) + ".OuterScope.InnerScope";
            if (mtag_stats[key_for_inner_scope + ".nCallsTo_malloc"] == 1
                && mtag_stats[key_for_inner_scope + ".nBytesSelf"] == MALLOC_AMOUNT)
                std::cout << "SUCCESS: Malloc-tag is aware of the memory allocation" << std::endl;
            else
                std::cout << "FAILURE: apparently the malloc() operation has NOT been served by malloc-tag"
                          << std::endl;
        }
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
