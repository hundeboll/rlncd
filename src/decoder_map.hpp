#pragma once

#include <unordered_map>
#include <unordered_set>
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
    typedef uint16_t key_type;
    typedef decoder::pointer value_type;
    typedef std::unordered_map<key_type, value_type> map_type;
    typedef std::pair<key_type, value_type> element_type;
    typedef std::unordered_set<key_type> invalid_type;

    decoder::factory m_factory;
    map_type m_decoders;
    invalid_type m_invalid_decoders;
    std::thread m_housekeeping;
    std::mutex m_decoders_lock;
    std::atomic<bool> m_running = {true};

    decoder::pointer create_decoder(key_type key);
    decoder::pointer get_decoder(key_type key);
    void housekeeping();
    void do_housekeeping();

  public:
    typedef std::shared_ptr<decoder_map> pointer;

    decoder_map() : m_factory(FLAGS_symbols, FLAGS_symbol_size)
    {}
    ~decoder_map();
    void add_enc(struct nl_msg *msg, struct nlattr **attrs);
    void init();

};
