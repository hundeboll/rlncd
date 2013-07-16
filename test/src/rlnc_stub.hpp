#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/mngt.h>
#include <netlink/cache.h>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "io.hpp"

#define IF_INDEX 96

class genl_family_stub
{
    struct nl_sock *m_nlsock;
    struct nl_cache *m_nlcache;
    struct nl_cb *m_nlcb;
    struct genl_family *m_nlfamily;

    std::thread m_recv_thread;
    std::mutex m_thread_lock;
    std::mutex m_cond_lock;
    std::condition_variable m_wait_cond;
    uint32_t m_nlfamily_id;
    std::atomic<size_t> m_pkt_count = {0}, m_plain_count = {0};
    std::atomic<size_t> m_last_type = {BATADV_HLP_C_NUM};
    bool m_running = {true}, m_prio_in_order = {true};

    void parse_thread()
    {
        while (true) {
            nl_recvmsgs_default(m_nlsock);

            std::lock_guard<std::mutex> guard(m_thread_lock);
            if (!m_running)
                break;
        }
    }

    static int parse_cb(struct nl_msg *msg, void *arg)
    {
        return ((class genl_family_stub *)arg)->parse_msg(msg);
    }

    int parse_msg(struct nl_msg *msg)
    {
        struct nlmsghdr *nlh = nlmsg_hdr(msg);
        struct genlmsghdr *gnlh = (struct genlmsghdr *)nlmsg_data(nlh);
        struct nlattr *attrs[BATADV_HLP_A_NUM];
        uint32_t type;

        genlmsg_parse(nlh, 0, attrs, BATADV_HLP_A_MAX, NULL);

        switch (gnlh->cmd) {
            case BATADV_HLP_C_REGISTER:
                m_pkt_count++;
                msg = nlmsg_alloc();
                genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ,
                            family(), 0, 0,
                            BATADV_HLP_C_REGISTER, 1);
                nla_put_u32(msg, BATADV_HLP_A_IFINDEX, IF_INDEX);
                nl_send_auto(m_nlsock, msg);
                break;

            case BATADV_HLP_C_FRAME:
                m_pkt_count++;

                if (attrs[BATADV_HLP_A_TYPE]) {
                    type = nla_get_u32(attrs[BATADV_HLP_A_TYPE]);

                    if (type > m_last_type)
                        m_prio_in_order = false;

                    if (type == PLAIN_PACKET)
                        m_plain_count--;

                    m_last_type = type;
                }
                break;
        }

        std::lock_guard<std::mutex> lock(m_cond_lock);
        m_wait_cond.notify_all();

        return NL_STOP;
    }

  public:
    genl_family_stub()
    {
        std::string name("batman_adv");

        m_nlcb = nl_cb_alloc(NL_CB_CUSTOM);
        ASSERT_TRUE(m_nlcb);

        m_nlsock = nl_socket_alloc_cb(m_nlcb);
        ASSERT_TRUE(m_nlsock);

        ASSERT_EQ(genl_connect(m_nlsock), 0);

        ASSERT_EQ(0, nl_socket_set_buffer_size(m_nlsock, 1048576, 1048576));

        ASSERT_EQ(0, genl_ctrl_alloc_cache(m_nlsock, &m_nlcache));

        m_nlfamily = genl_ctrl_search_by_name(m_nlcache, name.c_str());
        ASSERT_TRUE(m_nlfamily);
        m_nlfamily_id = genl_family_get_id(m_nlfamily);

        ASSERT_EQ(0, nl_cb_set(m_nlcb, NL_CB_MSG_IN, NL_CB_CUSTOM, parse_cb,
                               this));

        genl_send_simple(m_nlsock, m_nlfamily_id, BATADV_HLP_C_REGISTER, 1, 0);

        m_recv_thread = std::thread(std::bind(&genl_family_stub::parse_thread, this));
    }

    ~genl_family_stub()
    {
        m_thread_lock.lock();
        m_running = false;
        genl_send_simple(m_nlsock, 0, 0, 1, 0);
        m_thread_lock.unlock();

        m_recv_thread.join();
        nl_close(m_nlsock);
        nl_socket_free(m_nlsock);
        free(m_nlcb);
        free(m_nlcache);
        free(m_nlfamily);
    }

    void send_frames(size_t count)
    {
        struct nl_msg *msg;
         m_plain_count = count;

        while (count--) {
            msg  = nlmsg_alloc();
            genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family(),
                        0, 0, BATADV_HLP_C_FRAME, 1);
            nla_put_u8(msg, BATADV_HLP_A_TYPE, PLAIN_PACKET);
            nla_put_u32(msg, BATADV_HLP_A_FRAME, count);
            nl_send_auto(m_nlsock, msg);
        }
    }


    uint32_t family() const
    {
        return m_nlfamily_id;
    }

    bool prio_in_order() const
    {
        return m_prio_in_order;
    }

    bool plain_rx_count() const
    {
        return m_plain_count;
    }

    uint32_t packets() const
    {
        return m_pkt_count;
    }

    void reset_counters()
    {
        m_plain_count = 0;
        m_pkt_count = 0;
        m_last_type = BATADV_HLP_C_NUM;
    }

    template<typename func, class duration>
    void wait(func &cond, duration &sleep)
    {
        std::unique_lock<std::mutex> lock(m_cond_lock);
        m_wait_cond.wait_for(lock, sleep, cond);
    }
};
