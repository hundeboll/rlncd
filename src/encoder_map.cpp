#include <chrono>
#include <algorithm>

#include "logging.hpp"
#include "io.hpp"
#include "encoder_map.hpp"

void encoder_map::init(size_t encoder_num)
{
    std::lock_guard<std::mutex> lock(m_encoders_lock);

    VLOG(LOG_INIT) << "using " << encoder_num << " encoders";
    m_encoders.resize(encoder_num);

    for (size_t i = 0; i < encoder_num; ++i)
        m_free_encoders.push_back(i);

    next_encoder();
}

void encoder_map::signal_blocking(bool enable)
{
    struct nl_msg *msg = nlmsg_alloc();
    uint32_t type = enable ? BATADV_HLP_C_BLOCK : BATADV_HLP_C_UNBLOCK;

    if (!m_io)
        return;

    if (m_blocked == enable)
        return;

    VLOG(LOG_CTRL) << "signal blocking (" << enable << ")";
    genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, m_io->family(), 0, 0,
                type, 1);
    nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex());
    m_io->add_msg(PACKET_NUM, msg);
    m_blocked = enable;
}

encoder::pointer encoder_map::current_encoder()
{
    if (m_blocked)
        return encoder::pointer();

    return m_encoders[m_current_encoder];
}

void encoder_map::next_encoder()
{
    encoder::pointer enc;

    if (m_free_encoders.empty()) {
        signal_blocking(true);
        return;
    }

    m_current_encoder = m_free_encoders.front();
    m_free_encoders.pop_front();
    m_encoders[m_current_encoder] = encoder::pointer();
    m_encoders[m_current_encoder] = m_factory.build();
    m_encoders[m_current_encoder]->block(m_block_count++);
    m_encoders[m_current_encoder]->enc_id(m_current_encoder);
    m_encoders[m_current_encoder]->set_io(m_io);
    m_encoders[m_current_encoder]->counters(counters());
}

void encoder_map::free_encoder(uint8_t id)
{
    m_free_encoders.push_back(id);
    m_encoders[id] = encoder::pointer();

    if (!m_blocked)
        return;

    next_encoder();
    signal_blocking(false);
}

void encoder_map::add_plain(struct nl_msg *msg, struct nlattr **attrs)
{
    encoder::pointer enc;

    std::lock_guard<std::mutex> lock(m_encoders_lock);
    enc = current_encoder();
    if (!enc) {
        counters_increment("drop");
        VLOG(LOG_PKT) << "drop packet";
        return;
    }

    enc->add_plain(msg);

    if (enc->full())
        next_encoder();
}

void encoder_map::add_ack(struct nl_msg *msg, struct nlattr **attrs)
{
    uint16_t uid = nla_get_u16(attrs[BATADV_HLP_A_BLOCK]);
    uint8_t enc_id = uid_enc(uid);
    encoder::pointer enc;

    std::lock_guard<std::mutex> lock(m_encoders_lock);
    enc = m_encoders[enc_id];

    if (!enc || enc->uid() != uid)
        return;

    VLOG(LOG_CTRL) << "acked (enc: " << enc->enc_id()
                   << ", block: " << enc->block()
                   << ", pkts: " << enc->enc_packets() << ")";
    counters_increment("ack");
    enc = encoder::pointer();
    free_encoder(enc_id);
}

void encoder_map::add_req(struct nl_msg *msg, struct nlattr **attrs)
{
    uint16_t uid = nla_get_u16(attrs[BATADV_HLP_A_BLOCK]);
    uint8_t enc_id = uid_enc(uid);
    encoder::pointer enc;

    std::lock_guard<std::mutex> lock(m_encoders_lock);
    enc = m_encoders[enc_id];

    if (!enc || enc->uid() != uid)
        return;

    enc->add_req(msg);
}
