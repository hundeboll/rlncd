#include "decoder_map.hpp"
#include "logging.hpp"

decoder::pointer decoder_map::create_decoder(uint8_t id, uint8_t block)
{
    decoder::pointer dec = m_factory.build();
    dec->dec_id(id);
    dec->block(block);
    dec->set_io(m_io);
    dec->counters(counters());
    dec->ctrl_trackers(ctrl_trackers());

    return dec;
}

decoder::pointer decoder_map::get_decoder(uint8_t id, uint8_t block)
{
    uint8_t max = id + 1;

    if (m_decoders.size() < max) {
        m_decoders.resize(max);
        m_decoders[id] = create_decoder(id, block);
        return m_decoders[id];
    }

    if (!m_decoders[id]) {
        m_decoders[id] = create_decoder(id, block);
        return m_decoders[id];
    }

    if (m_decoders[id]->block() == block)
        return m_decoders[id];

    if (m_decoders[id]->block() > block && block != 0)
        return decoder::pointer();

    m_decoders[id] = create_decoder(id, block);

    return m_decoders[id];
}

void decoder_map::add_enc(struct nl_msg *msg, struct nlattr **attrs)
{
    uint16_t uid = nla_get_u16(attrs[BATADV_HLP_A_BLOCK]);
    uint8_t dec_id = uid_dec(uid);
    uint8_t block = uid_block(uid);
    decoder::pointer dec;

    std::lock_guard<std::mutex> lock(m_decoders_lock);
    dec = get_decoder(dec_id, block);

    if (!dec) {
        VLOG(LOG_PKT) << "dropping enc (block: " << static_cast<int>(block)
                      << ")";
        return;
    }

    VLOG(LOG_PKT) << "add enc (block: " << static_cast<int>(block) << ")";
    dec->add_enc(msg);
}
