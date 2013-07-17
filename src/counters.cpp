#include <map>
#include "counters.hpp"

int main()
{
    std::map<std::string, size_t> sorted_map;
    managed_shared_memory segment(open_only, SHM_NAME);
    counters::shared_map *mymap = segment.find<counters::shared_map>(SHM_MAP_NAME).first;

    for (auto i : *mymap)
        sorted_map[i.first.c_str()] = i.second;

    for (auto i : sorted_map)
        std::cout << i.first << ": " << i.second << std::endl;

    return EXIT_SUCCESS;
}
