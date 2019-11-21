#ifndef __CODEC_HPP__
#define __CODEC_HPP__

#include "thread_queue.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifdef __cplusplus
extern "C" {
#include <libavutil/rational.h>
#include <libavutil/pixdesc.h>
#include <libavutil/log.h>
}
#endif

struct AVDictionary;
struct AVFormatContext;
struct AVInputFormat;
struct AVOutputFormat;
struct AVCodecContext;
struct AVCodec;
struct SwsContext;
struct AVFrame;
struct AVPacket;
struct AVStream;
struct AVFilterGraph;
struct AVFilterContext;
struct AVRational;

using AVFramePtr = AVFrame *;
using AVPacketPtr = AVPacket *;

static const std::vector<uint8_t> prefix1 {0x00, 0x00, 0x00, 0x01};
static const std::vector<uint8_t> prefix2 {0x00, 0x00, 0x01};

struct TranscoderContext
{
    AVFormatContext *formatContext = nullptr;
    AVCodecContext *codecContext = nullptr;
    AVCodec *codec = nullptr;
    AVStream *videoStream = nullptr;
};

class RecordCodec
{

    using CallBackType = std::function<void(std::vector<uint8_t> &&)>;

public:
    explicit RecordCodec(std::string const &, std::string const &);
    ~RecordCodec();
    RecordCodec(const RecordCodec &) = delete;
    RecordCodec &operator=(const RecordCodec &) = delete;

    void Run();
    void Stop();
    void SetOnEncodedDataCallback(std::function<void(std::vector<uint8_t> &&)> callback);
    const bool Running() const;
    std::string Name() const;
    std::string RtspUrl() const;

private:
    void RegisterAll();
    void InitializeDecoder();
    void InitializeEncoder();
    void InitializeConverter();
    void InitFilters();
private:
    void EncodeFrame();
    int DecodePacketToFrame(AVCodecContext *, AVFrame *, AVPacket *);
    int EncodeFrameToPacket(AVCodecContext *, AVFrame *, AVPacket *);
    void CheckDelay();
    void RunDelay();
    void EncodeFrameToSend();
    void CleanDeque();
    void CleanUp();

private:
    std::string name_;

    std::string url_;
    size_t frame_width_;
    size_t frame_height_;
    AVPixelFormat raw_pix_fmt_;
    AVPixelFormat encoder_pix_fmt_;
    AVRational frame_rate_;
    size_t bit_rate_;
    TranscoderContext in_ctx_;
    TranscoderContext out_ctx_;
    AVFrame *raw_frame_;
    AVFrame *converted_frame_;
    AVFrame *filter_frame_;
    AVPacket *decoding_packet_;
    AVPacket *encoding_packet_;
    SwsContext *converter_ctx_;
    std::string filter_query_;
    AVFilterGraph *filter_fraph_;
    AVFilterContext *buffer_src_ctx_;
    AVFilterContext *buffer_sink_ctx_;
    std::atomic_bool stop_flag_;
    std::atomic_bool running_flag_;
    std::atomic_bool delay_;

    //
    ooknn::ThreadQueue<AVFrame *> deque_;
    CallBackType encode_cb_;
};

#endif  //
