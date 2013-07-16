#include <netlink/genl/genl.h>
#include <chrono>

#include "encoder.hpp"
#include "io-api.hpp"
#include "budgets.hpp"

DECLARE_int32(e1);
DECLARE_int32(e2);
DECLARE_int32(e3);
DECLARE_double(encoder_timeout);

namespace kodo {

template<>
void encoder::free_queue()
{
    struct nl_msg *msg;
    std::lock_guard<std::mutex> lock(m_queue_lock);

    while (m_msg_queue.size()) {
        msg = m_msg_queue.top();
        nlmsg_free(msg);
        m_msg_queue.pop();
    }
}

template<>
encoder::~full_rlnc_encoder_deep()
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

template<>
void encoder::send_encoded()
{
    struct nl_msg *msg;
    struct nlattr *attr;
    uint8_t *data;

    if (!m_io)
        return;

    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->family(),
                0, 0, BATADV_HLP_C_FRAME, 1);

    nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex());
    nla_put(msg, BATADV_HLP_A_SRC, ETH_ALEN, m_src);
    nla_put(msg, BATADV_HLP_A_DST, ETH_ALEN, m_dst);
    nla_put_u16(msg, BATADV_HLP_A_BLOCK, m_block);
    nla_put_u8(msg, BATADV_HLP_A_TYPE, ENC_PACKET);
    attr = nla_reserve(msg, BATADV_HLP_A_FRAME, this->payload_size());
    data = reinterpret_cast<uint8_t *>(nla_data(attr));

    this->encode(data);
    m_io->add_msg(ENC_PACKET, msg);
    m_credits -= m_credits >= 1 ? 1 : 0;
    m_enc_count++;
}

template<>
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
    VLOG(LOG_PKT) << "add plain (block: " << m_block
                  << ", rank: " << this->rank()
                  << ", credits: " << m_credits << ")";
}

template<>
void encoder::process_req(struct nl_msg *msg, struct nlattr **attrs)
{
    struct nlattr *attr;
    size_t rank, seq;

    attr = attrs[BATADV_HLP_A_RANK];
    rank = nla_get_u16(attr);
    attr = attrs[BATADV_HLP_A_SEQ];
    seq  = nla_get_u16(attr);

    if (rank == this->rank() || seq == m_last_req_seq) {
        VLOG(LOG_CTRL) << "dropping request (block: " << m_block
                       << ", his rank: " << rank
                       << ", our rank: " << this->rank()
                       << ", his seq: " << seq
                       << ", our seq: " << m_last_req_seq;
        return;
    }

    m_credits += source_budget(this->rank() - rank, 255, 255, m_e3);

    VLOG(LOG_CTRL) << "req (block: " << m_block
                   << ", his rank: " << rank
                   << ", our rank: " << this->rank()
                   << ", his seq: " << seq
                   << ", our seq: " << m_last_req_seq
                   << ", credits: " << m_credits
                   << ")";
    m_last_req_seq = seq;
}

template<>
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
            break;

        case REQ_PACKET:
            process_req(msg, attrs);
            break;

        default:
            LOG(ERROR) << "encoder received unknown type: " << type;
            break;
    }

    m_timestamp = timer::now();
}

template<>
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

template<>
void encoder::process_encoder()
{
    while (m_running && m_credits >= 1)
        send_encoded();

    if (this->rank() != this->symbols())
        return;

    while (m_running && m_enc_count < m_budget)
        send_encoded();
}

template<>
void encoder::process_timer()
{
    resolution diff;

    diff = std::chrono::duration_cast<resolution>(timer::now() - m_timestamp);

    if (diff.count() <= FLAGS_encoder_timeout*1000)
        return;

    if (this->rank() == 0)
        return;

    LOG(ERROR) << "timeout (block: " << m_block
               << ", rank: " << this->rank() << ")";
    m_running = false;
}

template<>
void encoder::thread_func()
{
    std::chrono::milliseconds interval(50);
    while (m_running) {
        process_queue();
        process_encoder();
        process_timer();

        std::unique_lock<std::mutex> lock(m_queue_lock);
        m_queue_cond.wait_for(lock, interval);
    }

    free_queue();
}

template<>
void encoder::init()
{
    m_e1 = FLAGS_e1*2.55;
    m_e2 = FLAGS_e2*2.55;
    m_e3 = FLAGS_e3*2.55;
    m_budget = source_budget(this->symbols(), m_e1, m_e2, m_e3);
    m_last_req_seq = 0;
    m_plain_count = 0;
    m_enc_count = 0;
    m_running = true;
    m_credits = 0;
    m_timestamp = timer::now();
    m_symbol_storage = new uint8_t[this->block_size()];

    VLOG(LOG_GEN) << "init (block: " << m_block
                  << ", budget: " << m_budget << ")";

    /* use locks to prevent data race due to reordering */
    std::lock_guard<std::mutex> queue_lock(m_queue_lock);
    m_thread = std::thread(std::bind(&encoder::thread_func, this));
}

template<>
void encoder::add_msg(uint8_t type, struct nl_msg *msg)
{
    nlmsg_get(msg);
    m_msg_queue.push(type, msg);
    m_queue_cond.notify_one();
}

template<>
void encoder::add_plain(struct nl_msg *msg)
{
    std::lock_guard<std::mutex> lock(m_queue_lock);
    m_plain_count++;
    add_msg(PLAIN_PACKET, msg);
}

};  // namepace kodo
