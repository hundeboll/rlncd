#pragma once

#include <atomic>
#include <memory>
#include <utility>
#include <mutex>
#include <vector>
#include <chrono>

#include "logging.hpp"

class ctrl_tracker
{
    std::atomic<size_t> m_count = {0};
    std::pair<size_t, size_t> m_rtt;
    std::mutex m_rtt_lock;

  public:
    typedef std::shared_ptr<ctrl_tracker> pointer;

    void wait()
    {
        m_count++;
        VLOG(LOG_CTRL) << "count++ = " << m_count;
    }

    void done()
    {
        m_count--;
        CHECK_GE(m_count, 0) << "negative count";
        VLOG(LOG_CTRL) << "count-- = " << m_count;
    }

    size_t waiting()
    {
        return m_count;
    }

    void add_rtt(size_t rtt)
    {
        std::lock_guard<std::mutex> lock(m_rtt_lock);
        m_rtt.first++;
        m_rtt.second += rtt;
    }

    size_t get_rtt()
    {
        std::lock_guard<std::mutex> lock(m_rtt_lock);
        return m_rtt.second/m_rtt.first;
    }
};

class ctrl_tracker_api
{
  public:
    enum TYPE {
        ACK,
        REQ,
        TYPE_NUM
    };

    enum STATES : bool {
        WAITING = true,
        ACTIVE = false,
    };

  private:
    typedef std::pair<ctrl_tracker::pointer, ctrl_tracker::pointer> tracker_pair;
    typedef std::chrono::high_resolution_clock timer;
    typedef timer::time_point timestamp;
    typedef std::chrono::milliseconds resolution;
    typedef std::chrono::duration<resolution> duration;

    std::vector<ctrl_tracker::pointer> m_trackers;
    std::vector<bool> m_states;
    std::vector<std::mutex> m_locks;
    std::vector<timestamp> m_timestamps;

    void update_timestamps(TYPE t)
    {
        using std::chrono::duration_cast;

        resolution diff;
        diff = duration_cast<resolution>(timer::now() - m_timestamps[t]);

        m_trackers[t]->add_rtt(diff.count());
        VLOG(LOG_CTRL) << "diff " << t << " rtt: "
                       << diff.count() << " ("
                       << m_trackers[t]->waiting() << ")";
        VLOG(LOG_CTRL) << "avg " << t << " rtt: "
                       << m_trackers[t]->get_rtt()
                       << " ms";
    }

    void wait(TYPE t)
    {
        std::lock_guard<std::mutex> lock(m_locks[t]);

        if (!m_trackers[t])
            return;

        if (m_states[t] == WAITING)
            return;

        m_trackers[t]->wait();
        m_states[t] = WAITING;
        m_timestamps[t] = timer::now();
    }

    void done(TYPE t)
    {
        std::lock_guard<std::mutex> lock(m_locks[t]);

        if (!m_trackers[t])
            return;

        if (m_states[t] == ACTIVE)
            return;

        m_trackers[t]->done();
        m_states[t] = ACTIVE;
        update_timestamps(t);
    }

    size_t waiting(TYPE t)
    {
        std::lock_guard<std::mutex> lock(m_locks[t]);

        return m_trackers[t] ? m_trackers[t]->waiting() : 0;
    }

  protected:
    void ack_wait()
    {
        wait(ACK);
    }

    void ack_done()
    {
        done(ACK);
    }

    size_t ack_waiting()
    {
        return waiting(ACK);
    }

    void req_wait()
    {
        wait(REQ);
    }

    void req_done()
    {
        done(REQ);
    }

    size_t req_waiting()
    {
        return waiting(REQ);
    }

  public:
    ctrl_tracker_api() :
        m_trackers(TYPE_NUM),
        m_locks(TYPE_NUM),
        m_timestamps(TYPE_NUM),
        m_states(TYPE_NUM, ACTIVE)
    {
    }

    void ctrl_trackers(TYPE t, ctrl_tracker::pointer &tracker)
    {
        m_trackers[t] = tracker;
    }

    void ctrl_trackers(std::vector<ctrl_tracker::pointer> &trackers)
    {
        m_trackers = trackers;
    }

    std::vector<ctrl_tracker::pointer> &ctrl_trackers()
    {
        return m_trackers;
    }
};
