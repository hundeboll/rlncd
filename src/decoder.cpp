#include "logging.hpp"
#include "budgets.hpp"
#include "decoder.hpp"

DECLARE_int32(e1);
DECLARE_int32(e2);
DECLARE_int32(e3);
DECLARE_double(decoder_timeout);
DECLARE_double(packet_timeout);

namespace kodo {

template<>
decoder::~full_rlnc_decoder_deep()
{
    m_running = false;

    m_queue_lock.lock();
    m_queue_cond.notify_all();
    m_queue_lock.unlock();

    if (m_thread.joinable())
        m_thread.join();
}

template<>
void decoder::send_dec(size_t index)
{
    struct nl_msg *msg;
    uint16_t len;
    uint8_t *buf;

    /* don't send already decoded packets */
    if (m_decoded_symbols[index])
        return;

    /* Read out length field from decoded data */
    buf = this->symbol(index);
    len = *reinterpret_cast<uint16_t *>(buf);

    /* avoid wrongly decoded packets by checking that the
     * length is within expected range
     */
    LOG_IF(FATAL, len > 1600) << "failed packet (block: " << m_block
                                          << ", index: " << index << ")";

    msg = CHECK_NOTNULL(nlmsg_alloc());
    CHECK_NOTNULL(genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->family(),
                              0, 0, BATADV_HLP_C_FRAME, 1));

    CHECK_EQ(nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex()), 0);
    CHECK_EQ(nla_put_u8(msg, BATADV_HLP_A_TYPE, DEC_PACKET), 0);
    CHECK_EQ(nla_put(msg, BATADV_HLP_A_FRAME, len, buf + sizeof(uint16_t)), 0);

    if (m_io)
        m_io->add_msg(DEC_PACKET, msg);
    else
        nlmsg_free(msg);

    VLOG(LOG_PKT) << "decoded (block: " << m_block
                  << ", index: " << index << ")";
    m_decoded_symbols[index] = true;
}

template<>
void decoder::process_enc(struct nl_msg *msg, struct nlattr **attrs)
{
    size_t rank = this->rank(), size = this->payload_size(), index;
    bool systematic;
    struct nlattr *attr;
    uint8_t *data;
    uint16_t len;

    if (this->is_complete())
        return;

    if (this->rank() == 0)
        read_address(attrs);

    /* get data from netlink message */
    attr = attrs[BATADV_HLP_A_FRAME];
    data = static_cast<uint8_t *>(nla_data(attr));
    len = nla_len(attr);
    CHECK_EQ(len, size) << "invalid length";

    this->decode(data);

    if (this->rank() == rank)
        VLOG(LOG_PKT) << "non-innovative (block: " << m_block
                      << ", rank: " << rank << ")";

    systematic = this->last_symbol_is_systematic();
    index = this->last_symbol_index();

    if (systematic) {
        send_dec(index);
        VLOG(LOG_PKT) << "systematic (block: " << m_block
                      << ", index: " << index
                      << ", rank: " << this->rank() << ")";
    } else {
        VLOG(LOG_PKT) << "encoded (block: " << m_block
                      << ", rank: " << m_rank << ")";
    }
}

template<>
void decoder::process_msg(struct nl_msg *msg)
{
    struct nlmsghdr *nlh = nlmsg_hdr(msg);
    struct genlmsghdr *gnlh = (struct genlmsghdr *)nlmsg_data(nlh);
    struct nlattr *attrs[BATADV_HLP_A_NUM], *attr;
    size_t type;

    genlmsg_parse(nlh, 0, attrs, BATADV_HLP_A_MAX, NULL);

    type = nla_get_u8(attrs[BATADV_HLP_A_TYPE]);
    switch (type) {
        case ENC_PACKET:
            process_enc(msg, attrs);
            break;

        default:
            LOG(ERROR) << "encoder received unknown type: " << type;
            break;
    }

    m_timestamp = timer::now();
}

template<>
void decoder::process_queue()
{
    struct nl_msg *msg;

    while (m_running) {
        {
            std::lock_guard<std::mutex> lock(m_queue_lock);

            if (m_msg_queue.empty())
                break;

            msg = m_msg_queue.top();
            m_msg_queue.pop();
        }

        process_msg(msg);

        if (m_io)
            m_io->free_msg(msg);
        else
            nlmsg_free(msg);
    }
}

template<>
void decoder::send_ack()
{
    double budget = source_budget(1, 255, 255, m_e3);
    struct nl_msg *msg;

    msg = CHECK_NOTNULL(nlmsg_alloc());
    CHECK_NOTNULL(genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->family(),
                              0, 0, BATADV_HLP_C_FRAME, 1));

    CHECK_EQ(nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex()), 0);
    CHECK_EQ(nla_put(msg, BATADV_HLP_A_SRC, ETH_ALEN, m_src), 0);
    CHECK_EQ(nla_put(msg, BATADV_HLP_A_DST, ETH_ALEN, m_dst), 0);
    CHECK_EQ(nla_put_u16(msg, BATADV_HLP_A_BLOCK, m_block), 0);
    CHECK_EQ(nla_put_u8(msg, BATADV_HLP_A_TYPE, ACK_PACKET), 0);
    CHECK_EQ(nla_put_u16(msg, BATADV_HLP_A_INT, 0), 0);

    VLOG(LOG_CTRL) << "ack (block: " << m_block
                   << ", budget: " << budget << ")";

    if (m_io) {
        for (; budget >= 0; --budget) {
            nlmsg_get(msg);
            m_io->add_msg(REQ_PACKET, msg);
        }
    }

    nlmsg_free(msg);
}

template<>
void decoder::send_req()
{
    double budget = source_budget(1, 255, 255, m_e3);
    struct nl_msg *msg;

    msg = CHECK_NOTNULL(nlmsg_alloc());
    CHECK_NOTNULL(genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->family(),
                              0, 0, BATADV_HLP_C_FRAME, 1));

    CHECK_EQ(nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex()), 0);
    CHECK_EQ(nla_put_u8(msg, BATADV_HLP_A_TYPE, REQ_PACKET), 0);
    CHECK_EQ(nla_put(msg, BATADV_HLP_A_SRC, ETH_ALEN, m_src), 0);
    CHECK_EQ(nla_put(msg, BATADV_HLP_A_DST, ETH_ALEN, m_dst), 0);
    CHECK_EQ(nla_put_u16(msg, BATADV_HLP_A_BLOCK, m_block), 0);
    CHECK_EQ(nla_put_u16(msg, BATADV_HLP_A_RANK, this->rank()), 0);
    CHECK_EQ(nla_put_u16(msg, BATADV_HLP_A_SEQ, m_req_seq), 0);

    VLOG(LOG_CTRL) << "req (block: " << m_block
                   << ", rank: " << this->rank()
                   << ", seq: " << m_req_seq
                   << ", budget: " << budget << ")";

    if (m_io) {
        for (; budget >= 0; --budget) {
            nlmsg_get(msg);
            m_io->add_msg(REQ_PACKET, msg);
        }
    }

    nlmsg_free(msg);
    m_req_seq++;
}

template<>
void decoder::process_decoder()
{
    if (this->is_complete()) {
        send_ack();

        for (size_t i = 0; i < this->symbols(); ++i)
            send_dec(i);

        m_running = false;
        VLOG(LOG_GEN) << "decoded (block: " << m_block << ")";

        return;
    }

    if (this->is_partial_complete()) {
        for (size_t i = 0; i < this->rank(); ++i)
            send_dec(i);

        return;
    }
}

template<>
void decoder::process_timer()
{
    size_t pkt_timeout = FLAGS_packet_timeout*1000;
    resolution diff;

    diff = std::chrono::duration_cast<resolution>(timer::now() - m_timestamp);

    if (diff.count() >= m_timeout) {
        LOG(ERROR) << "timeout (block: " << m_block
                   << ", rank: " << this->rank() << ")";
        m_running = false;
        return;
    }

    if (diff.count() >= pkt_timeout && !this->is_partial_complete()) {
        send_req();
        m_timeout -= pkt_timeout;
        m_timestamp = timer::now();
        return;
    }
}

template<>
void decoder::free_queue()
{
    struct nl_msg *msg;
    std::lock_guard<std::mutex> lock(m_queue_lock);

    while (m_msg_queue.size()) {
        msg = m_msg_queue.top();
        if (m_io)
            m_io->free_msg(msg);
        else
            nlmsg_free(msg);
        m_msg_queue.pop();
    }
}

template<>
void decoder::thread_func()
{
    std::chrono::milliseconds interval(50);

    while (m_running) {
        process_queue();
        process_decoder();
        process_timer();

        std::unique_lock<std::mutex> lock(m_queue_lock);
        m_queue_cond.wait_for(lock, interval);
    }

    free_queue();
}

template<>
void decoder::init()
{
    m_e1 = FLAGS_e1*2.55;
    m_e2 = FLAGS_e2*2.55;
    m_e3 = FLAGS_e3*2.55;
    m_running = true;
    m_req_seq = 1;
    m_enc_count = 0;
    m_timestamp = timer::now();
    m_timeout = FLAGS_decoder_timeout*1000;
    m_decoded_symbols.resize(this->symbols());
    std::fill(m_decoded_symbols.begin(), m_decoded_symbols.end(), false);

    std::lock_guard<std::mutex> lock(m_queue_lock);
    m_thread = std::thread(std::bind(&decoder::thread_func, this));
}

template<>
void decoder::add_msg(size_t type, struct nl_msg *msg)
{
    nlmsg_get(msg);
    m_msg_queue.push(type, msg);
    m_queue_cond.notify_one();
}

template<>
void decoder::add_enc(struct nl_msg *msg)
{
    std::lock_guard<std::mutex> lock(m_queue_lock);
    m_enc_count++;
    add_msg(ENC_PACKET, msg);
}

};  // namespace kodo
