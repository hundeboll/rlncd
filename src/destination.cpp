#include <glog/logging.h>
#include <gflags/gflags.h>
#include <gflags/gflags_completions.h>

#include "io.hpp"

DEFINE_string(interface, "bat0", "Name of interface to register");
DEFINE_int32(symbols, 64, "The generation size, the number of packets "
                                  "which are coded together.");
DEFINE_int32(symbol_size, 1454, "The payload size without RLNC overhead.");
DEFINE_bool(benchmark, false, "Bounce frames upon reception");
DEFINE_int32(encoders, 2, "Number of concurrent encoder.");
DEFINE_double(encoder_timeout, 1, "Time to wait for more packets before "
                                  "dropping encoder generation.");
DEFINE_double(fixed_overshoot, 1.06, "Fixed factor to increase "
                                     "encoder/recoder budgets.");
DEFINE_int32(e1, 10, "Error probability from source to helper in percentage.");
DEFINE_int32(e2, 10, "Error probability from helper to dest in percentage.");
DEFINE_int32(e3, 30, "Error probability from source to dest in percentage.");

int main(int argc, char **argv)
{
    std::string usage("ecode packets with Random Linear Network Coding\n");
    google::HandleCommandLineCompletions();
    google::SetUsageMessage(usage);
    google::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging(argv[0]);
    google::InstallFailureSignalHandler();

    io i;

    return 0;
}
