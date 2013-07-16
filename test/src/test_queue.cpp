#include <gtest/gtest.h>
#include <array>
#include "queue.hpp"

class queue_test : public ::testing::Test {
    typedef prio_queue<int> queue_type;
    int m_max_priority = {4}, m_last_priority = {m_max_priority};
    queue_type m_queue;

  protected:
    void fill(size_t num)
    {
        for (size_t i = 0; i < m_max_priority; ++i)
            for (size_t j = 0; j < num; ++j)
                m_queue.push(i, j);
    }

    void clear()
    {
        m_queue.clear();
    }

    void test_end()
    {
        queue_type::iterator it;
        size_t count = 0, num = 10;

        fill(num);
        for (it = m_queue.begin(); it != m_queue.end(); ++it)
            count++;
        clear();

        ASSERT_EQ(m_max_priority*num, count);
    }

    void test_clear()
    {
        size_t num = 10, count = 0;
        queue_type::iterator it;

        fill(num);
        m_queue.clear();
        for (it = m_queue.begin(); it != m_queue.end(); it++)
            count++;
        clear();

        ASSERT_EQ(0, count);
    }

    void test_sequence()
    {
        size_t num = 10;

        fill(num);
        queue_type::iterator it(m_queue.begin());
        for (size_t i = 0; i < m_max_priority; ++i) {
            for (size_t j = 0; j < num; ++j) {
                ASSERT_EQ(j, *it);
                it++;
            }
        }
        clear();
    }

    void test_mixed()
    {
        std::array<int, 10> priorities = {0,1,2,3,2,1,0,1,2,0};

        for (auto i : priorities)
            m_queue.push(i, i);

        for (auto it : m_queue) {
            ASSERT_LE(it, m_last_priority);
            m_last_priority = it;
        }
        clear();
        m_last_priority = m_max_priority;
    }

    void test_top()
    {
        std::array<int, 10> priorities = {0,1,2,3,2,1,0,1,2,0};

        for (auto i : priorities)
            m_queue.push(i, i);

        for (size_t i = 0; i < priorities.size(); ++i) {
            ASSERT_LE(m_queue.top(), m_last_priority);
            m_last_priority = m_queue.top();
            m_queue.pop();
        }
        clear();
        m_last_priority = m_max_priority;
    }

    void test_pop()
    {
        size_t num = 10, count = 0;

        fill(num);
        while (!m_queue.empty())
            m_queue.pop();

        ASSERT_EQ(0, m_queue.size());

        for (auto i : m_queue)
            count++;

        ASSERT_EQ(0, count);
        clear();
    }

    void test_next_priority()
    {
        size_t next_prio = m_max_priority / 2;
        m_queue.push(next_prio, 10);
        ASSERT_EQ(next_prio, m_queue.priority_next());
        clear();
    }

  public:
    queue_test() : m_queue(m_max_priority)
    {}
};

TEST_F(queue_test, iterator)
{
    test_end();
    test_sequence();
}

TEST_F(queue_test, order)
{
    test_top();
    test_pop();
    test_clear();
    test_mixed();
}

TEST_F(queue_test, api)
{
    test_next_priority();
}
