#pragma once

#include <netlink/msg.h>
#include <kodo/rlnc/full_vector_codes.hpp>
#include <kodo/storage_aware_generator.hpp>
#include <kodo/shallow_symbol_storage.hpp>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

#include "logging.hpp"
#include "queue.hpp"
#include "io-api.hpp"
#include "io.hpp"

namespace kodo {

template<class Field>
class full_rlnc_encoder_deep
    : public io_base,
      public
           // Payload Codec API
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
           final_coder_factory<
           // Final type
           full_rlnc_encoder_deep<Field
               > > > > > > > > > > > > > > > > > >
{
    typedef std::chrono::high_resolution_clock timer;
    typedef timer::time_point timestamp;
    typedef std::chrono::milliseconds resolution;
    typedef std::chrono::duration<resolution> duration;

    prio_queue<struct nl_msg *> m_msg_queue;
    std::thread m_thread;
    std::mutex m_queue_lock;
    std::condition_variable m_queue_cond;
    timestamp m_timestamp = {timer::now()};
    std::atomic<bool> m_running = {true};
    std::atomic<size_t> m_plain_count = {0}, m_enc_count = {0};
    std::atomic<size_t> m_last_req_seq = {0};
    std::atomic<uint8_t> m_e1, m_e2, m_e3;
    double m_credits = {0}, m_budget = {0};
    uint8_t *m_symbol_storage, m_src[ETH_ALEN], m_dst[ETH_ALEN];
    uint16_t m_block;

    void free_queue();
    void send_encoded();
    void process_plain(struct nl_msg *msg, struct nlattr **attrs);
    void process_req(struct nl_msg *msg, struct nlattr **attrs);
    void process_msg(struct nl_msg *msg);
    void process_queue();
    void process_encoder();
    void process_timer();
    void thread_func();
    void add_msg(uint8_t type, struct nl_msg *msg);

    uint8_t *get_symbol_buffer(size_t i)
    {
        return m_symbol_storage + i * this->symbol_size();
    }

  public:
    full_rlnc_encoder_deep() : m_msg_queue(PACKET_NUM, NULL)
    {}
    ~full_rlnc_encoder_deep();
    void init();
    void add_plain(struct nl_msg *msg);

    void exit()
    {
        std::lock_guard<std::mutex> lock(m_queue_lock);
        m_running = false;
        m_queue_cond.notify_all();
    }

    bool full() const
    {
        return m_plain_count == this->symbols();
    }

    bool running() const
    {
        return m_running;
    }

    size_t enc_packets() const
    {
        return m_enc_count;
    }

    uint16_t block()
    {
        return m_block;
    }

    void set_block(uint16_t block)
    {
        m_block = block;
    }

    void add_req(struct nl_msg *msg)
    {
        std::lock_guard<std::mutex> lock(m_queue_lock);
        add_msg(REQ_PACKET, msg);
    }

    void read_address(struct nlattr **attrs)
    {
        void *src = nla_data(attrs[BATADV_HLP_A_SRC]);
        void *dst = nla_data(attrs[BATADV_HLP_A_DST]);

        memcpy(m_src, src, ETH_ALEN);
        memcpy(m_dst, dst, ETH_ALEN);
    }
};

typedef full_rlnc_encoder_deep<fifi::binary8> encoder;

}  // namespace kodo
