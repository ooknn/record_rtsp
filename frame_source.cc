#include "codec.hpp"
#include "frame_source.hpp"
#include <iostream>
#include <assert.h>
#include <mutex>

RecordFrameSource *RecordFrameSource::createNew(UsageEnvironment &env, RecordCodecPtr codecer)
{
    return new RecordFrameSource(env, codecer);
}

RecordFrameSource::RecordFrameSource(UsageEnvironment &env, RecordCodecPtr codecer)
    : FramedSource(env)
    , codecer_(codecer)
    , event_id_(0)
    , max_nalu_size_(0)
{

    event_id_ = envir().taskScheduler().createEventTrigger(RecordFrameSource::DeliverFrame0);
    assert(event_id_ != 0);
    buffer_.reserve(5);
    codecer_->SetOnEncodedDataCallback(std::bind(&RecordFrameSource::OnEncodedData, this, std::placeholders::_1));
    std::cout << "Starting to capture and encode video from the camera: " << codecer_->RtspUrl() << std::endl;

    std::thread([&codecer = codecer_]() { codecer->Run(); }).detach();
}

RecordFrameSource::~RecordFrameSource()
{
    codecer_->Stop();
    envir().taskScheduler().deleteEventTrigger(event_id_);
    event_id_ = 0;
    buffer_.clear();
    std::cout << codecer_->Name() << ":max NALU size: " << max_nalu_size_ << std::endl;
}

void RecordFrameSource::OnEncodedData(std::vector<uint8_t> &&newData)
{

    if (!isCurrentlyAwaitingData())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.emplace_back(std::move(newData));
    }

    envir().taskScheduler().triggerEvent(event_id_, this);
}

void RecordFrameSource::DeliverFrame0(void *clientData)
{
    ((RecordFrameSource *)clientData)->DeliverData();
}

void RecordFrameSource::doStopGettingFrames()
{

    std::cout << "Stop getting frames from the camera: " << codecer_->Name() << std::endl;
    FramedSource::doStopGettingFrames();
}

void RecordFrameSource::DeliverData()
{

    if (!isCurrentlyAwaitingData())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> mu(mutex_);
        if (buffer_.empty())
        {
            return;
        }

        data_ = std::move(buffer_.back());

        buffer_.pop_back();
    }

    if (data_.size() > max_nalu_size_)
    {
        max_nalu_size_ = data_.size();
    }

    if (data_.size() > fMaxSize)
    {

        std::cout << "Exceeded max size, truncated: " << fNumTruncatedBytes << ", size: " << data_.size();
        std::cout << "\n";

        fFrameSize = fMaxSize;

        fNumTruncatedBytes = static_cast<unsigned int>(data_.size() - fMaxSize);
    }
    else
    {
        fFrameSize = static_cast<unsigned int>(data_.size());
    }

    std::cout << "encodedData size " << fFrameSize << std::endl;
    gettimeofday(&fPresentationTime, nullptr);
    memcpy(fTo, data_.data(), fFrameSize);
    FramedSource::afterGetting(this);
}

void RecordFrameSource::doGetNextFrame()
{

    if (!buffer_.empty())
    {
        DeliverData();
    }
    else
    {
        fFrameSize = 0;
        return;
    }
}
