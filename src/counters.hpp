/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_COUNTERS_HPP_
#define FOX_COUNTERS_HPP_

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/segment_manager.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/unordered_map.hpp>
#include <iostream>
#include <string>
#include <functional>
#include <mutex>

#define SHM_NAME "/rlncd_shared_memory"
#define SHM_MAP_NAME "counters"

using namespace boost::interprocess;

class counters {
    struct shm_remove
    {
        shm_remove() { shared_memory_object::remove(SHM_NAME); }
        ~shm_remove(){ shared_memory_object::remove(SHM_NAME); }
    } m_remover;

  public:
    typedef std::shared_ptr<counters> pointer;
    typedef allocator<char, managed_shared_memory::segment_manager>
        char_allocator;
    typedef basic_string<char, std::char_traits<char>, char_allocator>
        shm_string;
    typedef shm_string key_type;
    typedef size_t mapped_type;
    typedef std::pair<const key_type, mapped_type> value_type;
    typedef allocator<value_type, managed_shared_memory::segment_manager>
        shm_allocator;
    typedef boost::unordered_map<key_type, mapped_type, boost::hash<key_type>,
                                 std::equal_to<key_type>, shm_allocator>
                                     shared_map;

    managed_shared_memory m_segment;
    shm_allocator m_allocator;
    shared_map *m_counter_map;
    std::mutex m_lock;

    counters() :
        m_segment(create_only, SHM_NAME, 65536),
        m_allocator(m_segment.get_segment_manager()),
        m_counter_map(m_segment.construct<shared_map>(SHM_MAP_NAME)
                      (100, boost::hash<key_type>(), std::equal_to<key_type>(),
                       m_allocator))
    {}

    void increment(const std::string &key)
    {
        std::lock_guard<std::mutex> l(m_lock);
        (*m_counter_map)[shm_string(key.c_str(), m_allocator)]++;
    }

    void print()
    {
        std::lock_guard<std::mutex> l(m_lock);
        for (auto i : *m_counter_map)
            std::cout << i.first << ": " << i.second << std::endl;
    }
};

class counters_api
{
    counters::pointer m_counts;
    std::string m_group;

  protected:
    void counters_group(std::string group)
    {
        m_group = group;
    }

    void counters_increment(const char *str)
    {
        if (m_counts)
            m_counts->increment(m_group + " " + str);
    }

  public:
    void counters(counters::pointer counts)
    {
        m_counts = counts;
    }

    counters::pointer counters() const
    {
        return m_counts;
    }
};

#endif
