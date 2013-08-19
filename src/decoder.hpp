#pragma once

#include <kodo/rlnc/full_vector_codes.hpp>
#include <kodo/is_partial_complete.hpp>
#include "kodo/rank_info.hpp"
#include "kodo/payload_rank_decoder.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

#include "io.hpp"
#include "counters.hpp"
#include "queue.hpp"
#include "ctrl_tracker.hpp"
#include "systematic_decoder.hpp"

DECLARE_double(decoder_timeout);
DECLARE_double(req_timeout);
DECLARE_double(ack_timeout);

namespace kodo {

class decoder;

template<class Field>
class decoder_base
    : public
             partial_decoding_tracker<
             // Payload API
             payload_rank_decoder<
             payload_decoder<
             // Codec Header API
             systematic_decoder<
             systematic_decoder_info<
             symbol_id_decoder<
             // Symbol ID API
             plain_symbol_id_reader<
             // Codec API
             aligned_coefficients_decoder<
             forward_linear_block_decoder<
             rank_info<
             // Coefficient Storage API
             coefficient_storage<
             coefficient_info<
             // Storage API
             deep_symbol_storage<
             storage_bytes_used<
             storage_block_info<
             // Finite Field API
             finite_field_math<typename fifi::default_field<Field>::type,
             finite_field_info<Field,
             // Factory API
             final_coder_factory_pool<
             // Final type
             decoder
                 > > > > > > > > > > > > > > > > > >
{};

class decoder
  : public io_base,
    public counters_api,
    public ctrl_tracker_api,
    public decoder_base<fifi::binary8>
{
    typedef std::chrono::high_resolution_clock timer;
    typedef timer::time_point timestamp;
    typedef std::chrono::milliseconds resolution;
    typedef std::chrono::duration<resolution> duration;

    prio_queue<struct nl_msg *> m_msg_queue;
    timestamp m_timestamp;
    std::thread m_thread;
    std::mutex m_queue_lock, m_init_lock;
    std::condition_variable m_queue_cond;
    std::atomic<uint8_t> m_block, m_dec_id;
    std::atomic<size_t> m_enc_count;
    std::atomic<bool> m_running = {true}, m_decoded, m_idle;
    std::vector<bool> m_decoded_symbols;
    uint8_t m_src[ETH_ALEN], m_dst[ETH_ALEN];
    size_t m_req_seq, m_timeout, m_req_timeout, m_ack_timeout;

    void send_dec(size_t index);
    void send_ack();
    void send_req();
    void process_enc(struct nl_msg *msg, struct nlattr **attrs);
    void process_msg(struct nl_msg *msg);
    void process_queue();
    void process_decoder();
    void process_timer();
    void free_queue();
    void thread_func();
    void add_msg(size_t type, struct nl_msg *msg);

    void read_address(struct nlattr **attrs)
    {
        void *src = nla_data(attrs[BATADV_HLP_A_SRC]);
        void *dst = nla_data(attrs[BATADV_HLP_A_DST]);

        memcpy(m_src, src, ETH_ALEN);
        memcpy(m_dst, dst, ETH_ALEN);
    }

  public:
    decoder() : m_msg_queue(PACKET_NUM, NULL)
    {
        counters_group("decoder");
    }
    ~decoder();
    void add_enc(struct nl_msg *msg);

    template<class Factory>
    void construct(Factory &factory)
    {
        std::lock_guard<std::mutex> lock(m_init_lock);
        decoder_base::construct(factory);
        m_decoded_symbols.resize(factory.max_symbols());
        m_thread = std::thread(std::bind(&decoder::thread_func, this));
    }

    template<class Factory>
    void initialize(Factory &factory)
    {
        std::lock_guard<std::mutex> lock(m_init_lock);
        decoder_base::initialize(factory);
        counters_increment("generations");
        ack_done();

        m_decoded = false;
        m_idle = false;
        m_req_seq = 1;
        m_enc_count = 0;
        m_timestamp = timer::now();
        m_timeout = FLAGS_decoder_timeout*1000;
        m_req_timeout = FLAGS_req_timeout*1000;
        m_ack_timeout = FLAGS_ack_timeout*1000;
        std::fill(m_decoded_symbols.begin(), m_decoded_symbols.end(), false);
        free_queue();
    }

    void dec_id(uint8_t id)
    {
        m_dec_id = id;
    }

    void block(uint8_t block)
    {
        m_block = block;
    }

    size_t block() const
    {
        return m_block;
    }

    uint8_t dec_id() const
    {
        return m_dec_id;
    }

    uint16_t uid() const
    {
        return (m_dec_id << 8) | m_block;
    }
};

};  // namespace kodo
