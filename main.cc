#include "rtsp_server.hpp"
#include "codec.hpp"
#include <csignal>
#include <iostream>

namespace
{

std::function<void(int)> shutdown_handler;

void signal_handler(int signal)
{
    shutdown_handler(signal);
}
}  // namespace

int main(int argc, char **argv)
{

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGKILL, signal_handler);
    std::signal(SIGINT, signal_handler);


    av_log_set_level(AV_LOG_INFO);

    RecordCodec record("record", ":0.0");

    RecordRtspServer server;

    shutdown_handler = [&server](int signal) {
        std::cout << "Terminating server..." << std::endl;
        server.StopServer();
    };

    server.AddTranscoder(&record);

    server.Run();

    return 0;
}
