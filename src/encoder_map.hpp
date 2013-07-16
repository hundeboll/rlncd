#pragma once

#include <glog/logging.h>
#include <gflags/gflags.h>
#include <cstddef>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <memory>

#include "io.hpp"
#include "encoder.hpp"

DECLARE_int32(symbol_size);
DECLARE_int32(symbols);

using kodo::encoder;

class encoder_map : public io_base
{
    typedef uint16_t key_type;
    typedef encoder::pointer value_type;
    typedef std::unordered_map<key_type, value_type> map_type;
    typedef std::pair<key_type, value_type> element_type;
    encoder::factory m_factory;
    encoder::pointer m_current_encoder;
    map_type m_encoders;
    std::thread m_housekeeping;
    std::mutex m_encoders_lock;
    std::atomic<uint16_t> m_block_count = {0}, m_max_encoders = {1};
    std::atomic<bool> m_running = {true}, m_blocked = {false};

    encoder::pointer get_current_encoder();
    encoder::pointer create_encoder();
    void signal_blocking(bool enable);
    void do_housekeeping();
    void housekeeping();

  public:
    typedef std::shared_ptr<encoder_map> pointer;

    encoder_map() : m_factory(FLAGS_symbols, FLAGS_symbol_size) {}
    ~encoder_map();
    void add_plain(struct nl_msg *msg, struct nlattr **attrs);
    bool add_ack(struct nl_msg *msg, struct nlattr **attrs);
    bool add_req(struct nl_msg *msg, struct nlattr **attrs);
    void init(size_t encoder_num);
};
