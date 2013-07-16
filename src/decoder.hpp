#pragma once

#include <kodo/rlnc/full_vector_codes.hpp>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

#include "io.hpp"
#include "queue.hpp"
#include "systematic_decoder.hpp"

namespace kodo {

template<class Field>
class full_rlnc_decoder_deep
    : public io_base,
      public
             // Payload API
             payload_decoder<
             // Codec Header API
             systematic_decoder<
             systematic_decoder_info<
             symbol_id_decoder<
             // Symbol ID API
             plain_symbol_id_reader<
             // Codec API
             aligned_coefficients_decoder<
             linear_block_decoder<
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
             final_coder_factory<
             // Final type
             full_rlnc_decoder_deep<Field>
                 > > > > > > > > > > > > > > >
{
    typedef std::chrono::high_resolution_clock timer;
    typedef timer::time_point timestamp;
    typedef std::chrono::milliseconds resolution;
    typedef std::chrono::duration<resolution> duration;

    prio_queue<struct nl_msg *> m_msg_queue;
    timestamp m_timestamp;
    std::thread m_thread;
    std::mutex m_queue_lock;
    std::condition_variable m_queue_cond;
    std::atomic<uint16_t> m_block;
    std::atomic<size_t> m_enc_count;
    std::atomic<bool> m_running = {true};
    std::vector<bool> m_decoded_symbols;
    uint8_t m_e1, m_e2, m_e3, m_src[ETH_ALEN], m_dst[ETH_ALEN];
    size_t m_req_seq, m_timeout;

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
    full_rlnc_decoder_deep() : m_msg_queue(PACKET_NUM, NULL)
    {}
    ~full_rlnc_decoder_deep();
    void add_enc(struct nl_msg *msg);
    void init();

    void set_block(uint16_t block)
    {
        m_block = block;
    }

    uint16_t block()
    {
        return m_block;
    }

    bool running()
    {
        return m_running;
    }
};

typedef full_rlnc_decoder_deep<fifi::binary8> decoder;

};  // namespace kodo
