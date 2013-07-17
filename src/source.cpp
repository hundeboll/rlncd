#include <glog/logging.h>
#include <gflags/gflags.h>
#include <gflags/gflags_completions.h>

#include <signal.h>
#include <thread>
#include <chrono>
#include <atomic>

#include "io.hpp"
#include "encoder_map.hpp"
#include "decoder_map.hpp"

DEFINE_string(interface, "bat0", "Name of interface to register");
DEFINE_int32(symbols, 64, "The generation size, the number of packets "
                                  "which are coded together.");
DEFINE_int32(symbol_size, 1454, "The payload size without RLNC overhead.");
DEFINE_bool(benchmark, false, "Bounce frames upon reception");
DEFINE_int32(encoders, 2, "Number of concurrent encoder.");
DEFINE_double(encoder_timeout, 1, "Time to wait for more packets before "
                                  "dropping encoder generation.");
DEFINE_double(decoder_timeout, 1, "Time to wait for more packets before "
                                  "dropping decoder generation.");
DEFINE_double(packet_timeout, .3, "Time to wait for more packets before "
                                       "requesting more data");
DEFINE_double(fixed_overshoot, 1.06, "Fixed factor to increase "
                                     "encoder/recoder budgets.");
DEFINE_int32(e1, 99, "Error probability from source to helper in percentage.");
DEFINE_int32(e2, 99, "Error probability from helper to dest in percentage.");
DEFINE_int32(e3, 30, "Error probability from source to dest in percentage.");

static std::atomic<bool> running(true);

void sigint(int signal)
{
    running = false;
}

int main(int argc, char **argv)
{
    std::string usage("Encode packets with Random Linear Network Coding\n");
    FLAGS_alsologtostderr = true;
    FLAGS_colorlogtostderr = true;
    google::HandleCommandLineCompletions();
    google::SetUsageMessage(usage);
    google::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging(argv[0]);
    google::InstallFailureSignalHandler();

    signal(SIGINT, sigint);
    signal(SIGTERM, sigint);

    io::pointer i(new io);
    encoder_map::pointer enc_map(new encoder_map);
    decoder_map::pointer dec_map(new decoder_map);

    enc_map->set_io(i);
    enc_map->init(FLAGS_encoders);
    dec_map->set_io(i);

    i->set_encoder_map(enc_map);
    i->set_decoder_map(dec_map);
    i->netlink_open();
    i->netlink_register();
    i->start();

    std::chrono::milliseconds interval(100);
    while (running)
        std::this_thread::sleep_for(interval);

    LOG(INFO) << "source exit";
    enc_map.reset();
    dec_map.reset();
    i.reset();

    return 0;
}
