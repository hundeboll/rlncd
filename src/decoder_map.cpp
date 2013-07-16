#include "decoder_map.hpp"
#include "logging.hpp"

decoder::pointer decoder_map::create_decoder(key_type key)
{
    decoder::pointer dec = m_factory.build();
    dec->set_block(key);
    dec->set_io(m_io);
    dec->init();

    m_decoders[key] = dec;

    return dec;
}

decoder::pointer decoder_map::get_decoder(key_type key)
{
    invalid_type::iterator inv_it;
    map_type::iterator it(m_decoders.find(key));

    if (it != m_decoders.end())
        return it->second;

    inv_it = m_invalid_decoders.find(key);

    if (inv_it != m_invalid_decoders.end())
        return decoder::pointer();

    VLOG(LOG_GEN) << "creating decoder (block: " << key << ")";
    return create_decoder(key);
}

void decoder_map::add_enc(struct nl_msg *msg, struct nlattr **attrs)
{
    key_type key = nla_get_u16(attrs[BATADV_HLP_A_BLOCK]);
    decoder::pointer dec;

    std::lock_guard<std::mutex> lock(m_decoders_lock);
    dec = get_decoder(key);

    if (!dec) {
        VLOG(LOG_PKT) << "dropping enc (block: " << key << ")";
        return;
    }

    VLOG(LOG_PKT) << "add enc (block: " << key << ")";
    dec->add_enc(msg);
}

void decoder_map::do_housekeeping()
{
    std::lock_guard<std::mutex> lock(m_decoders_lock);

    for (auto it = std::begin(m_decoders); it != std::end(m_decoders);) {
        if (!it->second) {
            it = m_decoders.erase(it);
        } else if (it->second->running()) {
            ++it;
        } else {
            VLOG(LOG_GEN) << "erase decoder (block: " << it->second->block()
                          << ", rank: " << it->second->rank()
                          << ")";
            m_invalid_decoders.insert(it->second->block());
            it = m_decoders.erase(it);
        }
    }
}

void decoder_map::housekeeping()
{
    std::chrono::milliseconds interval(100);

    while (m_running) {
        std::this_thread::sleep_for(interval);
        do_housekeeping();
    }
}

decoder_map::~decoder_map()
{
    m_running = false;

    if (m_housekeeping.joinable())
        m_housekeeping.join();
}


void decoder_map::init()
{
    m_housekeeping = std::thread(std::bind(&decoder_map::housekeeping, this));
}
