#pragma once

#include <netlink/msg.h>
#include <kodo/rlnc/full_vector_codes.hpp>
#include <kodo/storage_aware_generator.hpp>
#include <kodo/shallow_symbol_storage.hpp>
#include "kodo/rank_info.hpp"
#include "kodo/payload_rank_encoder.hpp"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

#include "logging.hpp"
#include "counters.hpp"
#include "budgets.hpp"
#include "queue.hpp"
#include "io-api.hpp"
#include "io.hpp"

DECLARE_int32(e1);
DECLARE_int32(e2);
DECLARE_int32(e3);

namespace kodo {

class encoder;

template<class Field>
class encoder_base
    : public
           // Payload Codec API
           payload_rank_encoder<
           payload_encoder<
           // Codec Header API
           systematic_encoder<
           symbol_id_encoder<
           // Symbol ID API
           plain_symbol_id_writer<
           // Coefficient Generator API
           storage_aware_generator<
           uniform_generator<
           // Codec API
           encode_symbol_tracker<
           zero_symbol_encoder<
           linear_block_encoder<
           storage_aware_encoder<
           rank_info<
           // Coefficient Storage API
           coefficient_info<
           // Symbol Storage API
           mutable_shallow_symbol_storage<
           storage_bytes_used<
           storage_block_info<
           // Finite Field API
           finite_field_math<typename fifi::default_field<Field>::type,
           finite_field_info<Field,
           // Factory API
           final_coder_factory_pool<
           // Final type
           encoder> > > > > > > > > > > > > > > > > > >
{};

class encoder
  : public io_base,
    public counters_api,
    public encoder_base<fifi::binary>
{
    typedef std::chrono::high_resolution_clock timer;
    typedef timer::time_point timestamp;
    typedef std::chrono::milliseconds resolution;
    typedef std::chrono::duration<resolution> duration;

    prio_queue<struct nl_msg *> m_msg_queue;
    std::thread m_thread;
    std::mutex m_queue_lock, m_init_lock;
    std::condition_variable m_queue_cond;
    timestamp m_timestamp = {timer::now()};
    std::atomic<bool> m_running = {true};
    std::atomic<size_t> m_plain_count = {0}, m_enc_count = {0};
    std::atomic<size_t> m_last_req_seq = {0};
    std::atomic<uint8_t> m_e1, m_e2, m_e3;
    double m_credits = {0}, m_budget = {0};
    uint8_t *m_symbol_storage, m_src[ETH_ALEN], m_dst[ETH_ALEN];
    uint8_t m_block, m_encoder;

    void free_queue();
    void send_encoded();
    void process_plain(struct nl_msg *msg, struct nlattr **attrs);
    void process_req(struct nl_msg *msg, struct nlattr **attrs);
    void process_msg(struct nl_msg *msg);
    void process_queue();
    void process_encoder();
    void thread_func();
    void add_msg(uint8_t type, struct nl_msg *msg);

    uint8_t *get_symbol_buffer(size_t i)
    {
        return m_symbol_storage + i * this->symbol_size();
    }

    void read_address(struct nlattr **attrs)
    {
        void *src = nla_data(attrs[BATADV_HLP_A_SRC]);
        void *dst = nla_data(attrs[BATADV_HLP_A_DST]);

        memcpy(m_src, src, ETH_ALEN);
        memcpy(m_dst, dst, ETH_ALEN);
        counters_increment("generations");
    }

  public:
    encoder() : m_msg_queue(PACKET_NUM, NULL)
    {
        m_e1 = FLAGS_e1*2.55;
        m_e2 = FLAGS_e2*2.55;
        m_e3 = FLAGS_e3*2.55;
        counters_group("encoder");
    }

    ~encoder();
    void add_plain(struct nl_msg *msg);

    template<class Factory>
    void construct(Factory &factory)
    {
        size_t data_size = factory.max_symbols() * factory.max_symbol_size();

        std::lock_guard<std::mutex> lock(m_init_lock);
        encoder_base::construct(factory);
        m_symbol_storage = CHECK_NOTNULL(new uint8_t[data_size]);
        m_thread = std::thread(std::bind(&encoder::thread_func, this));

        LOG(INFO) << "constructed new encoder";
    }

    template<class Factory>
    void initialize(Factory &factory)
    {
        std::lock_guard<std::mutex> lock(m_init_lock);
        encoder_base::initialize(factory);

        m_budget = source_budget(this->symbols(), m_e1, m_e2, m_e3);
        m_timestamp = timer::now();
        m_last_req_seq = 0;
        m_plain_count = 0;
        m_enc_count = 0;
        m_credits = 0;
        free_queue();

        VLOG(LOG_GEN) << "init (block: " << block()
                      << ", budget: " << m_budget << ")";
    }

    bool full() const
    {
        return m_plain_count == this->symbols();
    }

    size_t block() const
    {
        return m_block;
    }

    size_t enc_id() const
    {
        return m_encoder;
    }

    uint16_t uid() const
    {
        return (m_encoder << 8) | m_block;
    }

    size_t enc_packets() const
    {
        return m_enc_count;
    }

    void block(uint8_t block)
    {
        m_block = block;
    }

    void enc_id(uint8_t enc)
    {
        m_encoder = enc;
    }

    void add_req(struct nl_msg *msg)
    {
        std::lock_guard<std::mutex> lock(m_queue_lock);
        add_msg(REQ_PACKET, msg);
    }
};

};  // namespace kodo
