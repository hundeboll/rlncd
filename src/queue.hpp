#pragma once

#include <netlink/netlink.h>
#include <boost/range/adaptor/reversed.hpp>
#include <deque>
#include <queue>

template<typename Value, typename Inner = std::deque<Value>, typename Outer = std::vector<Inner> >
class prio_queue
{
    Outer m_queues;
    size_t m_size = {0};
    Value m_default;

  public:
    prio_queue(size_t num) : m_queues(num)
    {}

    prio_queue(size_t num, Value defult) : m_queues(num), m_default(defult)
    {}

    void push(size_t prio, Value val)
    {
        m_queues[prio].push_back(val);
        m_size++;
    }

    void pop()
    {
        typename Outer::reverse_iterator it;
        for (it = m_queues.rbegin(); it != m_queues.rend(); ++it) {
            if (it->empty())
                continue;

            it->pop_front();
            m_size--;
            break;
        }
    }

    Value top() const
    {
        typename Outer::const_reverse_iterator it;

        for (it = m_queues.crbegin(); it != m_queues.crend(); ++it)
            if (!it->empty())
                return it->front();

        return m_default;
    }

    size_t priority_next() const
    {
        size_t ret = m_queues.size();
        typename Outer::const_reverse_iterator it;

        for (it = m_queues.crbegin(); it != m_queues.crend(); ++it) {
            --ret;
            if (!it->empty())
                return ret;
        }
        return ret;
    }

    size_t size() const
    {
        return m_size;
    }

    bool empty() const
    {
        return m_size == 0;
    }

    void clear()
    {
        typename Outer::iterator it;

        for (it = m_queues.begin(); it != m_queues.end(); ++it)
            it->clear();

        m_size = 0;
    }

    class iterator : std::iterator_traits<typename Inner::iterator>
    {
        typename Outer::reverse_iterator m_outer_it, m_outer_end;
        typename Inner::iterator m_inner_it, m_inner_end;

        void update()
        {
            while (m_outer_it != m_outer_end) {
                m_inner_it = m_outer_it->begin();
                m_inner_end = m_outer_it->end();

                if (m_inner_it == m_inner_end)
                    ++m_outer_it;
                else
                    break;
            }
        }

      public:
        typedef std::forward_iterator_tag iterator_catagory;

        iterator()
            : m_outer_it(),
              m_outer_end(),
              m_inner_it(),
              m_inner_end()
        {}

        iterator(typename Outer::reverse_iterator const outer_it,
                 typename Outer::reverse_iterator const outer_end)
            : m_outer_it(outer_it),
              m_outer_end(outer_end)
        {
            update();
        }

        iterator(const iterator &oth)
            : m_outer_it(oth.m_outer_it),
              m_outer_end(oth.m_outer_end),
              m_inner_it(oth.m_inner_it),
              m_inner_end(oth.m_inner_end)
        {}

        iterator operator=(const iterator &oth)
        {
            m_outer_it = oth.m_outer_it;
            m_outer_end = oth.m_outer_end;
            m_inner_it = oth.m_inner_it;
            m_inner_end = oth.m_inner_end;

            return *this;
        }

        bool operator==(const iterator &oth) const
        {
            bool end = m_outer_it == m_outer_end;
            bool oth_end = oth.m_outer_it == oth.m_outer_end;

            if (end && oth_end)
                return true;

            if (end != oth_end)
                return false;

            return m_outer_it == oth.m_outer_it &&
                   m_inner_it == oth.m_inner_it;
        }

        bool operator!=(const iterator &oth) const
        {
            return !operator==(oth);
        }

        iterator &operator++()
        {
            if (++m_inner_it == m_inner_end) {
                ++m_outer_it;
                update();
            }
            return *this;
        }

        iterator operator++(int)
        {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        Value &operator*() const
        {
            return *m_inner_it;
        }
    };

    iterator begin()
    {
        return iterator(m_queues.rbegin(), m_queues.rend());
    }

    iterator end()
    {
        return iterator(m_queues.rend(), m_queues.rend());
    }
};
