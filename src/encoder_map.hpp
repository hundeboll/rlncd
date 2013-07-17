#pragma once

#include <glog/logging.h>
#include <gflags/gflags.h>
#include <cstddef>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <deque>
#include <memory>

#include "io.hpp"
#include "encoder.hpp"

DECLARE_int32(symbol_size);
DECLARE_int32(symbols);

using kodo::encoder;

class encoder_map : public io_base
{
    encoder::factory m_factory;
    std::vector<encoder::pointer> m_encoders;
    std::deque<uint8_t> m_free_encoders;
    std::mutex m_encoders_lock;
    std::atomic<uint8_t> m_block_count = {0}, m_current_encoder = {0};
    std::atomic<bool> m_blocked = {false};

    encoder::pointer create_encoder(uint8_t id);
    encoder::pointer current_encoder();
    void next_encoder();
    void free_encoder(uint8_t id);
    void signal_blocking(bool enable);

    uint8_t uid_block(uint16_t uid)
    {
        return uid & 0xFF;
    }

    uint8_t uid_enc(uint16_t uid)
    {
        return uid >> 8;
    }

  public:
    typedef std::shared_ptr<encoder_map> pointer;

    encoder_map() : m_factory(FLAGS_symbols, FLAGS_symbol_size) {}
    void add_plain(struct nl_msg *msg, struct nlattr **attrs);
    void add_ack(struct nl_msg *msg, struct nlattr **attrs);
    void add_req(struct nl_msg *msg, struct nlattr **attrs);
    void init(size_t encoder_num);
};
