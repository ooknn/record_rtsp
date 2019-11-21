#ifndef __RTSP_SERVER_HPP__
#define __RTSP_SERVER_HPP__

#include <vector>
#include <string>

class UsageEnvironment;
class TaskScheduler;
class RTSPServer;
class RecordCodec;
class FramedSource;

using RecordCodecPtr = RecordCodec *;
using FramedSourcePtr = FramedSource *;

class RecordRtspServer
{

public:
    unsigned int estimatedBitrate = (5000000 + 500) / 1000;
    constexpr static unsigned int DEFAULT_RTSP_PORT_NUMBER = 8554;
    explicit RecordRtspServer(unsigned int port = DEFAULT_RTSP_PORT_NUMBER);
    ~RecordRtspServer();
    void StopServer();
    void AddTranscoder(const RecordCodecPtr);
    void Run();

private:
    using RecordCodecArr = std::vector<RecordCodecPtr>;
    using FramedSourceArr = std::vector<FramedSourcePtr>;

    char stop_;
    unsigned int port_;
    TaskScheduler *scheduler_;
    UsageEnvironment *env_;
    RTSPServer *server_;
    RecordCodecArr record_coders_;
    FramedSourceArr video_sources_;
    void AddMediaSession(RecordCodec *, const std::string &, const std::string &);
};

#endif  // __RTSP_SERVER_HPP__
