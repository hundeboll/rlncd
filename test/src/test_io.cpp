#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <thread>
#include <chrono>
#include "io.hpp"
#include "rlnc_stub.hpp"

DEFINE_string(interface, "bat0", "Name of interface to register");
DEFINE_int32(symbols, 64, "The generation size, the number of packets "
                                  "which are coded together.");
DEFINE_int32(symbol_size, 1454, "The payload size without RLNC overhead.");
DEFINE_bool(benchmark, false, "Bounce frames upon reception");
DEFINE_int32(encoders, 2, "Number of concurrent encoder.");
DEFINE_double(encoder_timeout, 1, "Time to wait for more packets before "
                                  "dropping encoder generation.");
DEFINE_double(decoder_timeout, 1, "Time to wait for more packets before "
                                  "dropping decoder generation.");
DEFINE_double(packet_timeout, .3, "Time to wait for more packets before "
                                       "requesting more data");
DEFINE_double(fixed_overshoot, 1.06, "Fixed factor to increase "
                                     "encoder/recoder budgets.");
DEFINE_int32(e1, 10, "Error probability from source to helper in percentage.");
DEFINE_int32(e2, 10, "Error probability from helper to dest in percentage.");
DEFINE_int32(e3, 30, "Error probability from source to dest in percentage.");

class io_test : public ::testing::Test {
    io *m_io;
    genl_family_stub *m_stub;

    void add_frame(uint8_t type)
    {
        struct nl_msg *msg;

        msg = nlmsg_alloc();
        genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->family(),
                    0, 0, BATADV_HLP_C_FRAME, 1);
        nla_put_u8(msg, BATADV_HLP_A_TYPE, type);

        m_io->add_msg_unlocked(type, msg);
    }

  protected:
    virtual void SetUp()
    {
        m_stub = new genl_family_stub();
        m_io = new io();
    }

    void register_nl()
    {
        m_io->reset_counters();
        m_stub->reset_counters();
        m_io->netlink_open();
        m_io->start();
        m_io->netlink_register();
    }

    void wait_register()
    {
        auto l = std::bind([](io_test *i) { return i->m_io->packets() == 1; }, this);
        std::chrono::seconds s(2);
        m_io->wait(l, s);
        ASSERT_EQ(1, m_io->packets());
        ASSERT_EQ(IF_INDEX, m_io->ifindex());
    }

    void fill_queue()
    {
        m_io->reset_counters();
        m_stub->reset_counters();
        m_io->write_lock();
        add_frame(0);
        add_frame(2);
        add_frame(1);
        m_io->write_unlock();
    }

    void wait_queue()
    {
        auto l = std::bind([](io_test *i) {return i->m_stub->packets() == 3; }, this);
        std::chrono::seconds s(2);
        m_stub->wait(l, s);
        ASSERT_EQ(3, m_stub->packets());
        ASSERT_TRUE(m_stub->prio_in_order());
    }

    void send_plain()
    {
        FLAGS_benchmark = true;
        m_io->reset_counters();
        m_stub->reset_counters();
        m_stub->send_frames(10);
    }

    void wait_plain()
    {
        auto l = std::bind([](io_test *i) {return i->m_stub->packets() == 10; }, this);
        std::chrono::seconds s(2);
        m_stub->wait(l, s);
        ASSERT_EQ(10, m_io->packets());
        ASSERT_EQ(10, m_stub->packets());
        ASSERT_EQ(0, m_stub->plain_rx_count());
    }
};

TEST_F(io_test, queue_prio)
{
    register_nl();
    wait_register();

    fill_queue();
    wait_queue();

    send_plain();
    wait_plain();
}
