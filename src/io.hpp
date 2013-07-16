#pragma once

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/mngt.h>

#include <thread>
#include <mutex>
#include <atomic>
#include <deque>
#include <queue>
#include <condition_variable>
#include <memory>

#include "queue.hpp"
#include "io-api.hpp"

class encoder_map;
typedef std::weak_ptr<encoder_map> encoder_map_ptr;

class io
{
    /* Members for generic netlink */
    struct nl_sock *m_nlsock = {NULL};
    struct nl_cache *m_nlcache = {NULL};
    struct nl_cb *m_nlcb = {NULL};
    struct genl_family *m_nlfamily = {NULL};
    std::atomic<uint32_t> m_ifindex, m_nlfamily_id, m_pkt_count = {0};

    /* Members for thread handling */
    std::atomic<bool> m_running = {true};
    std::thread m_reader, m_writer;
    std::mutex m_read_lock, m_write_lock, m_free_lock, m_cond_lock;
    std::condition_variable m_write_cond, m_wait_cond;

    void bounce_frame(struct nlattr **attrs);
    void handle_frame(struct nl_msg *msg, struct nlattr **attrs);
    void process_free_queue();
    void read_thread();
    void write_thread();
    int read_msg(struct nl_msg *msg, void *arg);

    /* Producer/consumber members */
    prio_queue<struct nl_msg *> m_write_queue, m_free_queue;
    encoder_map_ptr m_encoder_map;

    static int read_wrapper(struct nl_msg *msg, void *arg)
    {
        return ((class io *)arg)->read_msg(msg, NULL);
    }

  public:
    typedef std::shared_ptr<io> pointer;

    io() : m_write_queue(PACKET_NUM + 1, NULL), m_free_queue(1, NULL)
    {}
    ~io();
    void start();
    void netlink_open();
    void netlink_register();
    void write_lock();
    void write_unlock();
    void add_msg_unlocked(uint8_t type, struct nl_msg *msg);
    void add_msg(uint8_t type, struct nl_msg *msg);
    void free_msg(struct nl_msg *msg);

    template<typename func, class duration>
    void wait(func &cond, duration &sleep)
    {
        std::unique_lock<std::mutex> lock(m_cond_lock);
        m_wait_cond.wait_for(lock, sleep, cond);
    }

    void set_encoder_map(encoder_map_ptr enc)
    {
        m_encoder_map = enc;
    }

    uint32_t family() const
    {
        return m_nlfamily_id.load();
    }

    uint32_t ifindex() const
    {
        return m_ifindex;
    }

    void reset_counters()
    {
        m_pkt_count = 0;
    }

    uint32_t packets() const
    {
        return m_pkt_count;
    }
};

class io_base
{
  protected:
      io::pointer m_io;

  public:
    void set_io(io::pointer io)
    {
        m_io = io;
    }
};
