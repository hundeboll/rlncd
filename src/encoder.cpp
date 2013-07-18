#include <netlink/genl/genl.h>
#include <chrono>

#include "encoder.hpp"
#include "io-api.hpp"

DECLARE_double(encoder_timeout);

namespace kodo {

void encoder::free_queue()
{
    struct nl_msg *msg;
    std::lock_guard<std::mutex> lock(m_queue_lock);

    while (m_msg_queue.size()) {
        msg = m_msg_queue.top();
        nlmsg_free(msg);
        m_msg_queue.pop();
        counters_increment("free");
    }
}

encoder::~encoder()
{
    m_running = false;

    m_queue_lock.lock();
    m_queue_cond.notify_all();
    m_queue_lock.unlock();

    if (m_thread.joinable())
        m_thread.join();

    if (m_symbol_storage)
        delete[] m_symbol_storage;
}

void encoder::send_encoded()
{
    struct nl_msg *msg;
    struct nlattr *attr;
    uint8_t *data;

    if (!m_io)
        return;

    msg = CHECK_NOTNULL(nlmsg_alloc());
    CHECK_NOTNULL(genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->family(),
                              0, 0, BATADV_HLP_C_FRAME, 1));

    CHECK_EQ(nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex()), 0);
    CHECK_EQ(nla_put(msg, BATADV_HLP_A_SRC, ETH_ALEN, m_src), 0);
    CHECK_EQ(nla_put(msg, BATADV_HLP_A_DST, ETH_ALEN, m_dst), 0);
    CHECK_EQ(nla_put_u16(msg, BATADV_HLP_A_BLOCK, uid()), 0);
    CHECK_EQ(nla_put_u8(msg, BATADV_HLP_A_TYPE, ENC_PACKET), 0);
    attr = CHECK_NOTNULL(nla_reserve(msg, BATADV_HLP_A_FRAME,
                                     this->payload_size()));
    data = reinterpret_cast<uint8_t *>(nla_data(attr));

    this->encode(data);
    m_io->add_msg(ENC_PACKET, msg);
    m_credits -= m_credits >= 1 ? 1 : 0;
    m_enc_count++;
    counters_increment("enc");
}

void encoder::process_plain(struct nl_msg *msg, struct nlattr **attrs)
{
    struct nlattr *attr;
    uint8_t *data, *buf;
    uint16_t len;

    /* get data from netlink message */
    attr = attrs[BATADV_HLP_A_FRAME];
    data = static_cast<uint8_t *>(nla_data(attr));
    len = nla_len(attr);

    if (this->rank() == 0)
        read_address(attrs);

    /* set length and add data to encoder */
    buf = get_symbol_buffer(this->rank());
    *reinterpret_cast<uint16_t *>(buf) = len;
    memcpy(buf + sizeof(len), data, len);
    sak::mutable_storage symbol(buf, this->symbol_size());
    this->set_symbol(this->rank(), symbol);

    /* increment credits to send encoded packets */
    m_credits += source_credit(m_e1, m_e2, m_e3);
    VLOG(LOG_PKT) << "add plain (block: " << block()
                  << ", rank: " << this->rank()
                  << ", credits: " << m_credits << ")";
}

void encoder::process_req(struct nl_msg *msg, struct nlattr **attrs)
{
    struct nlattr *attr;
    size_t rank, seq;

    attr = attrs[BATADV_HLP_A_RANK];
    rank = nla_get_u16(attr);
    attr = attrs[BATADV_HLP_A_SEQ];
    seq  = nla_get_u16(attr);

    if (rank == this->rank() || seq == m_last_req_seq) {
        VLOG(LOG_CTRL) << "dropping request (block: " << block()
                       << ", his rank: " << rank
                       << ", our rank: " << this->rank()
                       << ", his seq: " << seq
                       << ", our seq: " << m_last_req_seq;
        return;
    }

    m_credits += source_budget(this->rank() - rank, 255, 255, m_e3);

    VLOG(LOG_CTRL) << "req (block: " << block()
                   << ", his rank: " << rank
                   << ", our rank: " << this->rank()
                   << ", his seq: " << seq
                   << ", our seq: " << m_last_req_seq
                   << ", credits: " << m_credits
                   << ")";
    m_last_req_seq = seq;
}

void encoder::process_msg(struct nl_msg *msg)
{
    struct nlmsghdr *nlh = nlmsg_hdr(msg);
    struct genlmsghdr *gnlh = (struct genlmsghdr *)nlmsg_data(nlh);
    struct nlattr *attrs[BATADV_HLP_A_NUM], *attr;
    size_t type;

    genlmsg_parse(nlh, 0, attrs, BATADV_HLP_A_MAX, NULL);

    type = nla_get_u8(attrs[BATADV_HLP_A_TYPE]);
    switch (type) {
        case PLAIN_PACKET:
            process_plain(msg, attrs);
            counters_increment("plain");
            break;

        case REQ_PACKET:
            process_req(msg, attrs);
            counters_increment("req");
            break;

        default:
            LOG(ERROR) << "encoder received unknown type: " << type;
            break;
    }

    m_timestamp = timer::now();
}

void encoder::process_queue()
{
    struct nl_msg *msg;

    while (m_running) {
        std::lock_guard<std::mutex> lock(m_queue_lock);

        if (m_msg_queue.empty())
            break;

        msg = m_msg_queue.top();
        m_msg_queue.pop();

        process_msg(msg);

        if (m_io)
            m_io->free_msg(msg);
        else
            nlmsg_free(msg);
    }
}

void encoder::process_encoder()
{
    while (m_running && m_credits >= 1)
        send_encoded();

    if (this->rank() != this->symbols())
        return;

    while (m_running && m_enc_count < m_budget)
        send_encoded();
}

void encoder::thread_func()
{
    std::chrono::milliseconds interval(50);
    while (m_running) {
        process_queue();
        process_encoder();

        std::unique_lock<std::mutex> lock(m_queue_lock);
        m_queue_cond.wait_for(lock, interval);
    }

    free_queue();
}

void encoder::add_msg(uint8_t type, struct nl_msg *msg)
{
    nlmsg_get(msg);
    m_msg_queue.push(type, msg);
    m_queue_cond.notify_one();
}

void encoder::add_plain(struct nl_msg *msg)
{
    std::lock_guard<std::mutex> lock(m_queue_lock);
    m_plain_count++;
    add_msg(PLAIN_PACKET, msg);
}

};  // namepace kodo
