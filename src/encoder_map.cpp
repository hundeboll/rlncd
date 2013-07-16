#include <chrono>
#include <algorithm>

#include "logging.hpp"
#include "io.hpp"
#include "encoder_map.hpp"

void encoder_map::init(size_t encoder_num)
{
    std::lock_guard<std::mutex> lock(m_encoders_lock);

    VLOG(LOG_INIT) << "using " << encoder_num << " encoders";
    m_max_encoders = encoder_num;
    if (!m_current_encoder)
        create_encoder();

    m_housekeeping = std::thread(std::bind(&encoder_map::housekeeping, this));
}

encoder_map::~encoder_map()
{
    m_running = false;

    if (m_housekeeping.joinable())
        m_housekeeping.join();
}

void encoder_map::do_housekeeping()
{
    std::lock_guard<std::mutex> lock(m_encoders_lock);

    if (m_current_encoder && !m_current_encoder->running())
        m_current_encoder.reset();

    for (auto it = std::begin(m_encoders); it != std::end(m_encoders);) {
        if (!it->second) {
            it = m_encoders.erase(it);
        } else if (it->second->running()) {
            ++it;
        } else {
            VLOG(LOG_GEN) << "erase encoder (block: " << it->second->block()
                          << ", rank: " << it->second->rank()
                          << ")";
            it = m_encoders.erase(it);
        }
    }

    if (!m_current_encoder)
        create_encoder();
}

void encoder_map::housekeeping()
{
    std::chrono::milliseconds interval(100);

    while (m_running) {
        std::this_thread::sleep_for(interval);
        do_housekeeping();
    }
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

encoder::pointer encoder_map::get_current_encoder()
{
    if (m_current_encoder)
        return m_current_encoder;

    return create_encoder();
}

encoder::pointer encoder_map::create_encoder()
{
    if (m_encoders.size() >= m_max_encoders) {
        m_current_encoder = encoder::pointer();
        signal_blocking(true);

        return m_current_encoder;
    }

    VLOG(LOG_GEN) << "create new encoder (" << (m_block_count + 1) << ")";
    signal_blocking(false);
    m_current_encoder = m_factory.build();
    m_current_encoder->set_block(++m_block_count);
    m_current_encoder->set_io(m_io);
    m_current_encoder->init();
    m_encoders[m_block_count] = m_current_encoder;

    return m_current_encoder;
}

void encoder_map::add_plain(struct nl_msg *msg, struct nlattr **attrs)
{
    encoder::pointer enc;

    std::lock_guard<std::mutex> lock(m_encoders_lock);
    enc = get_current_encoder();
    if (!enc) {
        VLOG(LOG_PKT) << "drop packet";
        return;
    }

    enc->add_plain(msg);

    if (enc->full())
        create_encoder();
}

bool encoder_map::add_ack(struct nl_msg *msg, struct nlattr **attrs)
{
    uint16_t block_id = nla_get_u16(attrs[BATADV_HLP_A_BLOCK]);
    encoder::pointer e;

    std::lock_guard<std::mutex> lock(m_encoders_lock);
    e = m_encoders[block_id];

    if (e) {
        m_encoders.erase(block_id);
        VLOG(LOG_CTRL) << "acked (block: " << block_id
                       << ", pkts: " << e->enc_packets()
                       << ")";
    }

    if (!m_current_encoder)
        create_encoder();

    return true;
}

bool encoder_map::add_req(struct nl_msg *msg, struct nlattr **attrs)
{
    uint16_t block_id = nla_get_u16(attrs[BATADV_HLP_A_BLOCK]);

    std::lock_guard<std::mutex> lock(m_encoders_lock);

    map_type::iterator it = m_encoders.find(block_id);
    if (it == m_encoders.end()) {
        VLOG(LOG_CTRL) << "dropping req (block: " << block_id << ")";
        return false;
    }

    it->second->add_req(msg);
    return true;
}
