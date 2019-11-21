#include "rtsp_server.hpp"
#include "sub_session.hpp"
#include "codec.hpp"
#include "frame_source.hpp"

#include <UsageEnvironment.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <liveMedia.hh>
#include <vector>
#include <iostream>

RecordRtspServer::RecordRtspServer(unsigned int port)
    : port_(port)
    , stop_(0)
    , scheduler_(nullptr)
    , env_(nullptr)
    , server_(nullptr)
{

    OutPacketBuffer::maxSize = 10 * 1024 * 1024;
    std::cout << "Setting OutPacketBuffer max size to " << OutPacketBuffer::maxSize << " (bytes)" << std::endl;
    scheduler_ = BasicTaskScheduler::createNew();
    env_ = BasicUsageEnvironment::createNew(*scheduler_);
}

RecordRtspServer::~RecordRtspServer()
{

    Medium::close(server_);  // deletes all server media sessions

    // delete all framed sources
    for (const auto &src : video_sources_)
    {
        if (src)
        {
            Medium::close(src);
        }
    }

    env_->reclaim();

    delete scheduler_;

    record_coders_.clear();
    video_sources_.clear();
    stop_ = 0;

    std::cout << "RTSP server has been destructed!" << std::endl;
}

void RecordRtspServer::StopServer()
{

    std::cout << "Stop server " << std::endl;
    stop_ = 's';
}

void RecordRtspServer::AddTranscoder(RecordCodecPtr codec_ptr)
{
    record_coders_.push_back(codec_ptr);
}

void RecordRtspServer::Run()
{

    assert(!server_);
    server_ = RTSPServer::createNew(*env_, port_);
    assert(server_);

    std::cout << "Server has been created on port " << port_ << std::endl;

    for (auto &transcoder : record_coders_)
    {
        AddMediaSession(transcoder, transcoder->Name(), "stream description");
    }

    env_->taskScheduler().doEventLoop(&stop_);  // do not return
}

void RecordRtspServer::AddMediaSession(RecordCodec *transcoder, const std::string &streamName, const std::string &streamDesc)
{

    assert(OutPacketBuffer::maxSize > 5 * 1024 * 1024);

    std::cout << "Adding media session for camera: " << transcoder->Name() << std::endl;
    auto framedSource = RecordFrameSource::createNew(*env_, transcoder);
    video_sources_.push_back(framedSource);
    auto replicator = StreamReplicator::createNew(*env_, framedSource, False);
    auto sms = ServerMediaSession::createNew(*env_, streamName.c_str(), "stream information", streamDesc.c_str(), False, "a=fmtp:96\n");
    sms->addSubsession(RecordServerMediaSubsession::createNew(*env_, replicator, estimatedBitrate));
    server_->addServerMediaSession(sms);
    auto url = server_->rtspURL(sms);
    std::cout << "Play the stream of the '" << transcoder->Name() << "' camera using the following URL: " << url << std::endl;
    delete[] url;
}
