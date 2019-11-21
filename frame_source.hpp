#ifndef __FRAME_SOURCE_HPP__
#define __FRAME_SOURCE_HPP__

#include <FramedSource.hh>
#include <UsageEnvironment.hh>
#include <mutex>
#include <thread>
#include <vector>

class RecordCodec;
using RecordCodecPtr = RecordCodec *;

class RecordFrameSource : public FramedSource
{
public:
    static RecordFrameSource *createNew(UsageEnvironment &env, RecordCodecPtr);

protected:
    RecordFrameSource(UsageEnvironment &env, RecordCodecPtr);
    ~RecordFrameSource() override;
    void doGetNextFrame() override;
    void doStopGettingFrames() override;

private:
    using EncodeData = std::vector<uint8_t>;
    using EncodeDataBuffer = std::vector<EncodeData>;
    RecordCodec *codecer_;
    EventTriggerId event_id_;
    std::mutex mutex_;
    EncodeDataBuffer buffer_;
    EncodeData data_;
    size_t max_nalu_size_;
    void OnEncodedData(std::vector<uint8_t> &&data);
    void DeliverData();
    static void DeliverFrame0(void *);
};

#endif
