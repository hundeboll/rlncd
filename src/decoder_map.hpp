#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <memory>
#include "io.hpp"
#include "decoder.hpp"

using kodo::decoder;

DECLARE_int32(symbol_size);
DECLARE_int32(symbols);

class decoder_map : public io_base
{
    std::vector<decoder::pointer> m_decoders;
    std::mutex m_decoders_lock;
    decoder::factory m_factory;

    decoder::pointer create_decoder(uint8_t id, uint8_t block);
    decoder::pointer get_decoder(uint8_t id, uint8_t block);

    uint8_t uid_dec(uint16_t uid) const
    {
        return uid >> 8;
    }

    uint8_t uid_block(uint16_t uid) const
    {
        return uid & 0xFF;
    }

  public:
    typedef std::shared_ptr<decoder_map> pointer;

    decoder_map() : m_factory(FLAGS_symbols, FLAGS_symbol_size)
    {}
    void add_enc(struct nl_msg *msg, struct nlattr **attrs);
};
