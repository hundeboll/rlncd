#include <gflags/gflags.h>
#include <sys/socket.h>
#include <functional>
#include <iostream>
#include "logging.hpp"
#include "encoder_map.hpp"
#include "decoder_map.hpp"
#include "io.hpp"

DECLARE_string(interface);
DECLARE_bool(benchmark);
DECLARE_int32(encoders);
DECLARE_int32(e1);
DECLARE_int32(e2);
DECLARE_int32(e3);

io::~io()
{
    VLOG(LOG_INIT) << "destructor start";
    std::unique_lock<std::mutex> write_lock(m_write_lock);
    m_running = false;
    m_write_cond.notify_all();
    write_lock.unlock();

    if (m_writer.joinable())
        m_writer.join();

    if (m_nlsock)
        genl_send_simple(m_nlsock, family(), BATADV_HLP_C_UNSPEC, 1, 0);

    if (m_reader.joinable())
        m_reader.join();

    if (m_nlsock)
        nl_socket_free(m_nlsock);

    if (m_nlcb)
        free(m_nlcb);

    if (m_nlcache)
        free(m_nlcache);

    if (m_nlfamily)
        free(m_nlfamily);

    write_lock.lock();
    while (m_write_queue.size() > 0) {
        nlmsg_free(m_write_queue.top());
        m_write_queue.pop();
    }

    process_free_queue();
    VLOG(LOG_INIT) << "destructor end";
}

void io::start()
{
    if (!m_nlsock)
        return;

    std::lock_guard<std::mutex> read_lock(m_read_lock);
    m_reader = std::thread(std::bind(&io::read_thread, this));

    std::lock_guard<std::mutex> write_lock(m_write_lock);
    m_writer = std::thread(std::bind(&io::write_thread, this));
}

void io::netlink_open()
{
    std::string name("batman_adv");

    m_nlcb = CHECK_NOTNULL(nl_cb_alloc(NL_CB_CUSTOM));

    m_nlsock = CHECK_NOTNULL(nl_socket_alloc_cb(m_nlcb));

    CHECK_GE(genl_connect(m_nlsock), 0)
        << "io: Failed to connect netlink socket";

    CHECK_GE(nl_socket_set_buffer_size(m_nlsock, 1048576, 1048576), 0)
        << "IO: Unable to set socket buffer size";

    CHECK_GE(genl_ctrl_alloc_cache(m_nlsock, &m_nlcache), 0)
        << "IO: Failed to allocate control cache";

    m_nlfamily = CHECK_NOTNULL(genl_ctrl_search_by_name(m_nlcache,
                                                        name.c_str()));
    m_nlfamily_id = genl_family_get_id(m_nlfamily);

    nl_cb_set(m_nlcb, NL_CB_MSG_IN, NL_CB_CUSTOM, read_wrapper, this);
}

void io::netlink_register()
{
    struct nl_msg *msg = CHECK_NOTNULL(nlmsg_alloc());

    CHECK_NOTNULL(genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, family(), 0,
                NLM_F_REQUEST, BATADV_HLP_C_REGISTER, 1));

    CHECK_GE(nla_put_string(msg, BATADV_HLP_A_IFNAME, FLAGS_interface.c_str()), 0)
        << "IO: Failed to put ifname attribute";

    CHECK_GE(nla_put_u32(msg, BATADV_HLP_A_ENCS, FLAGS_encoders), 0)
            << "IO: Failed to put encoders attribute";

    CHECK_GE(nla_put_u32(msg, BATADV_HLP_A_E1, FLAGS_e1), 0)
            << "IO: Failed to put e1 attribute";

    CHECK_GE(nla_put_u32(msg, BATADV_HLP_A_E2, FLAGS_e2), 0)
            << "IO: Failed to put e2 attribute";

    CHECK_GE(nla_put_u32(msg, BATADV_HLP_A_E3, FLAGS_e3), 0)
            << "IO: Failed to put e3 attribute";

    add_msg(PACKET_NUM, msg);
}

void io::bounce_frame(struct nlattr **attrs)
{
    void *tmp = nla_data(attrs[BATADV_HLP_A_FRAME]);
    uint8_t *data = reinterpret_cast<uint8_t *>(tmp);
    size_t len = nla_len(attrs[BATADV_HLP_A_FRAME]);
    struct nl_msg *msg = nlmsg_alloc();

    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family(),
                0, 0, BATADV_HLP_C_FRAME, 1);

    nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_ifindex);
    nla_put_u8(msg, BATADV_HLP_A_TYPE, PLAIN_PACKET);
    nla_put(msg, BATADV_HLP_A_FRAME, len, data);
    add_msg(PLAIN_PACKET, msg);
}

void io::handle_frame(struct nl_msg *msg, struct nlattr **attrs)
{
    switch (nla_get_u8(attrs[BATADV_HLP_A_TYPE])) {
        case PLAIN_PACKET:
            if (auto encoder_map = m_encoder_map.lock())
                encoder_map->add_plain(msg, attrs);
            break;

        case ENC_PACKET:
            if (auto decoder_map = m_decoder_map.lock())
                decoder_map->add_enc(msg, attrs);
            break;

/*
        case HLP_PACKET:
            helpers_add_enc(msg, attrs);
            break;

        case REC_PACKET:
            relays_add_enc(msg, attrs);
            break;
*/
        case REQ_PACKET:
            if (auto encoder_map = m_encoder_map.lock())
                encoder_map->add_req(msg, attrs);
            break;

        case ACK_PACKET:
            if (auto encoder_map = m_encoder_map.lock())
                encoder_map->add_ack(msg, attrs);
            break;
    }
}

int io::read_msg(struct nl_msg *msg, void *arg)
{
    struct nlmsghdr *nlh = nlmsg_hdr(msg);
    struct genlmsghdr *gnlh = (struct genlmsghdr *)nlmsg_data(nlh);
    struct nlattr *attrs[BATADV_HLP_A_NUM];

    genlmsg_parse(nlh, 0, attrs, BATADV_HLP_A_MAX, NULL);

    switch (gnlh->cmd) {
        case BATADV_HLP_C_REGISTER:
            VLOG(LOG_IO) << "received register message";
            m_pkt_count++;

            if (attrs[BATADV_HLP_A_IFINDEX])
                m_ifindex = nla_get_u32(attrs[BATADV_HLP_A_IFINDEX]);

            break;

        case BATADV_HLP_C_FRAME:
            VLOG(LOG_IO) << "received frame message";
            m_pkt_count++;

            if (FLAGS_benchmark)
                bounce_frame(attrs);
            else
                handle_frame(msg, attrs);

            break;
    }

    std::lock_guard<std::mutex> lock(m_cond_lock);
    m_wait_cond.notify_all();

    return NL_STOP;
}

void io::process_free_queue()
{
    struct nl_msg *msg;
    std::lock_guard<std::mutex> lock(m_free_lock);

    while (m_free_queue.size()) {
        VLOG(LOG_IO) << "free message";
        msg = m_free_queue.top();
        m_free_queue.pop();
        nlmsg_free(msg);
    }
}

void io::read_thread()
{
    int ret(0);

    while (m_running) {
        ret = nl_recvmsgs_default(m_nlsock);
        LOG_IF(ERROR, ret < 0) << "Netlink read error: " << nl_geterror(ret)
                               << " (" << ret << ")";
        process_free_queue();
    }
    VLOG(LOG_INIT) << "read exit";
}

void io::write_thread()
{
    struct nl_msg *msg;
    int res, len;

    while (true) {
        std::unique_lock<std::mutex> l(m_write_lock);
        while (m_running && m_write_queue.empty())
            m_write_cond.wait(l);

        if (!m_running)
            break;

        msg = m_write_queue.top();
        m_write_queue.pop();

        l.unlock();

        if (!msg)
            continue;

        len = nlmsg_total_size(nlmsg_datalen(nlmsg_hdr(msg)));
        LOG_IF(ERROR, len > 1600 || len < 0)
            << "message too long ("
            << ", length: " << len
            << ", msg *: " << msg << ")";

        if (m_nlsock) {
            res = nl_send_auto(m_nlsock, msg);
            LOG_IF(ERROR, res < 0) << "nl_send_auto() failed with " << res
                                   << ": " << nl_geterror(res) << " (" << errno
                                   << ": " << strerror(errno) << ")";
            LOG_IF(ERROR, res < 0 && errno == 90) << "length too long: "
                << nlmsg_total_size(nlmsg_datalen(nlmsg_hdr(msg)));
        }

        if (res >= 0 || m_nlsock == NULL)
            nlmsg_free(msg);
    }

    VLOG(LOG_INIT) << "write exit";
}

void io::write_lock()
{
    m_write_lock.lock();
}

void io::write_unlock()
{
    m_write_cond.notify_one();
    m_write_lock.unlock();
}

void io::add_msg_unlocked(uint8_t type, struct nl_msg *msg)
{
    m_write_queue.push(type, msg);
}

void io::add_msg(uint8_t type, struct nl_msg *msg)
{
    write_lock();
    add_msg_unlocked(type, msg);
    write_unlock();
}

void io::free_msg(struct nl_msg *msg)
{
    std::lock_guard<std::mutex> lock(m_free_lock);
    m_free_queue.push(0, msg);
}
