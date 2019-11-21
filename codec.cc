#include "codec.hpp"
#include "scoped_exit.hpp"

#ifdef __cplusplus
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}
#endif

#include <iostream>
#include <assert.h>
#include <chrono>
#include <iterator>

#define WIDTH 1920
#define HEIGHT 1080

#define STOP_LOOP_BREAK                                                \
    {                                                                  \
        if (stop_flag_.load())                                         \
        {                                                              \
            std::cout << "needToStopFlag " << stop_flag_ << std::endl; \
            break;                                                     \
        }                                                              \
    }

#define DELAY_LOOP                                                      \
    {                                                                   \
        if (delay_.load())                                              \
        {                                                               \
            std::this_thread::sleep_for(std::chrono::milliseconds(30)); \
        }                                                               \
    }

#define ERROR_BREAK(x)                                \
    {                                                 \
        if (x == AVERROR(EAGAIN) || x == AVERROR_EOF) \
        {                                             \
            break;                                    \
        }                                             \
        assert(x >= 0);                               \
    }

RecordCodec::~RecordCodec()
{

    Stop();
    std::cout << "Transcoder destructed: " << name_ << std::endl;
}

RecordCodec::RecordCodec(std::string const &cameraName, std::string const &cameraUrl)
    : name_(cameraName)
    , url_(cameraUrl)
    , stop_flag_(false)
    , running_flag_(false)
{

    std::cout << "Constructing transcoder for " << cameraUrl;

    // get the pixel format enum
    this->raw_pix_fmt_ = av_get_pix_fmt("yuv420p");
    this->encoder_pix_fmt_ = av_get_pix_fmt("yuv420p");
    assert(raw_pix_fmt_ != AV_PIX_FMT_NONE && encoder_pix_fmt_ != AV_PIX_FMT_NONE);

    std::cout << "Set pixel formats of the camera original/codec: "
              << "yuv420p"
              << "/"
              << "yuv420p" << std::endl;

    //set framerate
    frame_rate_ = (AVRational) {static_cast<int>(25), 1};

    RegisterAll();

    InitializeDecoder();

    InitializeEncoder();

    InitializeConverter();

    InitFilters();
}

static void WriteFile(AVCodecContext *codecContext, AVFrame *frame, FILE *fp)
{

    int y_size = codecContext->width * codecContext->height;
    fwrite(frame->data[0], 1, y_size, fp);      //Y
    fwrite(frame->data[1], 1, y_size / 4, fp);  //U
    fwrite(frame->data[2], 1, y_size / 4, fp);  //V
}

void RecordCodec::CheckDelay()
{
    if (!delay_.load())
    {
        if (deque_.Size() > 100)
        {
            delay_.store(true);
        }
    }
    if (delay_.load())
    {
        if (deque_.Size() < 24)
        {
            delay_.store(false);
        }
    }
}

void RecordCodec::EncodeFrameToSend()
{
    auto p = deque_.Pop();
    if (!p)
    {
        return;
    }

    auto p_clean = make_scoped_exit([&p]() { av_frame_free(&p); });

    if (EncodeFrameToPacket(out_ctx_.codecContext, p, encoding_packet_) < 0)
    {
        return;
    }
    auto pkt_clean = make_scoped_exit([&p = encoding_packet_]() { av_packet_unref(p); });

    if (!encode_cb_)
    {
        return;
    }

    static uint32_t skip = 4;
    std::vector<uint8_t> data(encoding_packet_->data, encoding_packet_->data + encoding_packet_->size);
    if (std::equal(prefix1.begin(), prefix1.end(), data.begin()))
    {
        skip = prefix1.size();
    }
    else if (std::equal(prefix2.begin(), prefix2.end(), data.begin()))
    {
        skip = prefix2.size();
    }
    else
    {
        std::for_each(data.begin(), data.begin() + 4, [](uint8_t ch) { printf("%x ", ch); });
        printf("\n");
        assert(false);
    }
    data.erase(data.begin(), data.begin() + skip);
    encode_cb_(std::move(data));
}

void RecordCodec::EncodeFrame()
{
    while (true)
    {
        if (stop_flag_.load())
        {
            break;
        }
        CheckDelay();
        EncodeFrameToSend();
    }

    CleanDeque();
}

void RecordCodec::CleanDeque()
{
    while (!deque_.Empty())
    {
        auto p = deque_.Pop();
        if (p)
        {
            av_frame_free(&p);
        }
    }
}

void RecordCodec::Run()
{
    delay_.store(false);
    running_flag_.store(true);

    std::thread t([&]() { EncodeFrame(); });
    auto thread_clean = make_scoped_exit([&t = t]() { t.join(); });

    while (!stop_flag_.load())
    {
        if (av_read_frame(in_ctx_.formatContext, decoding_packet_) < 0)
        {
            break;
        }
        auto pkt_clean = make_scoped_exit([&pkt = decoding_packet_]() { av_packet_unref(pkt); });
        if (decoding_packet_->stream_index != in_ctx_.videoStream->index)
        {
            continue;
        }
        if (!DecodePacketToFrame(in_ctx_.codecContext, raw_frame_, decoding_packet_))
        {
            continue;
        }

        auto frame_clean = make_scoped_exit([&frame = raw_frame_]() { av_frame_unref(frame); });

        int statusCode = av_buffersrc_add_frame_flags(buffer_src_ctx_, raw_frame_, AV_BUFFERSRC_FLAG_KEEP_REF);
        if (statusCode < 0)
        {
            continue;
        }
        while (true)
        {
            STOP_LOOP_BREAK;

            DELAY_LOOP;

            statusCode = av_buffersink_get_frame(buffer_sink_ctx_, filter_frame_);

            ERROR_BREAK(statusCode);

            auto filter_clean = make_scoped_exit([&filter = filter_frame_]() { av_frame_unref(filter); });

            av_frame_make_writable(converted_frame_);
            sws_scale(converter_ctx_, reinterpret_cast<const uint8_t *const *>(filter_frame_->data), filter_frame_->linesize, 0, static_cast<int>(frame_height_), converted_frame_->data, converted_frame_->linesize);
            av_frame_copy_props(converted_frame_, filter_frame_);
            AVFrame *cp = av_frame_clone(converted_frame_);
            deque_.Push(std::move(cp));
        }
    }

    AVFrame *p = NULL;
    deque_.Push(std::move(p));
    running_flag_.store(false);
}

void RecordCodec::Stop()
{

    if (!running_flag_.load())
        return;

    stop_flag_.store(true);

    while (running_flag_.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    CleanUp();
}

void RecordCodec::RegisterAll()
{

    std::cout << "Registering ffmpeg stuff";

    avdevice_register_all();
}

void RecordCodec::InitializeDecoder()
{

    std::cout << "Initialize decoder of the camera " << url_;

    in_ctx_.formatContext = avformat_alloc_context();
    std::cout << "Using Video4Linux2 API for decoding raw data";

    AVInputFormat *inputFormat = av_find_input_format("x11grab");
    AVDictionary *options = nullptr;

    std::string s = std::to_string(WIDTH);
    s += "*";
    s += std::to_string(HEIGHT);
    av_dict_set(&options, "video_size", s.data(), 0);
    av_dict_set(&options, "pixel_format", av_get_pix_fmt_name(raw_pix_fmt_), 0);
    av_dict_set(&options, "framerate", "25", 0);

    int statCode = avformat_open_input(&in_ctx_.formatContext, url_.data(), inputFormat, &options);
    av_dict_free(&options);
    assert(statCode == 0);

    statCode = avformat_find_stream_info(in_ctx_.formatContext, nullptr);
    assert(statCode >= 0);
    av_dump_format(in_ctx_.formatContext, 0, name_.data(), 0);

    int videoStreamIndex = av_find_best_stream(in_ctx_.formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &in_ctx_.codec, 0);
    assert(videoStreamIndex >= 0);
    assert(in_ctx_.codec);
    in_ctx_.videoStream = in_ctx_.formatContext->streams[videoStreamIndex];
    in_ctx_.codecContext = avcodec_alloc_context3(in_ctx_.codec);
    assert(in_ctx_.codecContext);
    statCode = avcodec_parameters_to_context(in_ctx_.codecContext, in_ctx_.videoStream->codecpar);
    assert(statCode >= 0);
    in_ctx_.codecContext->thread_count = 8;
    statCode = avcodec_open2(in_ctx_.codecContext, in_ctx_.codec, &options);
    assert(statCode == 0);
    frame_rate_ = in_ctx_.videoStream->r_frame_rate;
    frame_width_ = static_cast<size_t>(in_ctx_.codecContext->width);
    frame_height_ = static_cast<size_t>(in_ctx_.codecContext->height);
    raw_pix_fmt_ = in_ctx_.codecContext->pix_fmt;
    bit_rate_ = static_cast<size_t>(in_ctx_.codecContext->bit_rate);

    std::cout << "Decoder params: width: " << frame_width_ << ", height: " << frame_height_ << ", pixel_fmt: "
              << av_get_pix_fmt_name(raw_pix_fmt_) << ", framerate: " << frame_rate_.num << std::endl;

    decoding_packet_ = av_packet_alloc();
    av_init_packet(decoding_packet_);
    raw_frame_ = av_frame_alloc();
}

void RecordCodec::InitializeEncoder()
{

    std::cout << "Initialize HEVC encoder" << std::endl;

    int statCode = avformat_alloc_output_context2(&out_ctx_.formatContext, nullptr, "null", nullptr);
    assert(statCode >= 0);

    out_ctx_.codec = avcodec_find_encoder_by_name("libx264");
    assert(out_ctx_.codec);

    out_ctx_.videoStream = avformat_new_stream(out_ctx_.formatContext, out_ctx_.codec);
    assert(out_ctx_.videoStream);
    out_ctx_.videoStream->id = out_ctx_.formatContext->nb_streams - 1;

    out_ctx_.codecContext = avcodec_alloc_context3(out_ctx_.codec);
    assert(out_ctx_.codecContext);

    out_ctx_.codecContext->width = static_cast<int>(WIDTH);
    out_ctx_.codecContext->height = static_cast<int>(HEIGHT);

    out_ctx_.codecContext->time_base = (AVRational) {1, static_cast<int>(25)};
    out_ctx_.codecContext->framerate = (AVRational) {static_cast<int>(25), 1};

    out_ctx_.codecContext->pix_fmt = encoder_pix_fmt_;
    if (out_ctx_.formatContext->flags & AVFMT_GLOBALHEADER)
    {
        out_ctx_.codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    avcodec_parameters_from_context(out_ctx_.videoStream->codecpar, out_ctx_.codecContext);
    AVDictionary *options = nullptr;
    av_dict_set(&options, "preset", "ultrafast", 0);
    statCode = avcodec_open2(out_ctx_.codecContext, out_ctx_.codec, NULL);
    av_dict_free(&options);
    assert(statCode == 0);
    statCode = avformat_write_header(out_ctx_.formatContext, nullptr);
    assert(statCode >= 0);

    av_dump_format(out_ctx_.formatContext, out_ctx_.videoStream->index, "null", 1);

    encoding_packet_ = av_packet_alloc();
    av_init_packet(encoding_packet_);
}

void RecordCodec::InitializeConverter()
{

    converted_frame_ = av_frame_alloc();
    converted_frame_->width = static_cast<int>(WIDTH);
    converted_frame_->height = static_cast<int>(HEIGHT);
    converted_frame_->format = encoder_pix_fmt_;
    int statCode = av_frame_get_buffer(converted_frame_, 0);  // ref counted frame
    assert(statCode == 0);

    // create converter from raw pixel format to encoder supported pixel format
    converter_ctx_ = sws_getCachedContext(nullptr, static_cast<int>(frame_width_), static_cast<int>(frame_height_), raw_pix_fmt_, converted_frame_->width, converted_frame_->height, encoder_pix_fmt_, SWS_LANCZOS, nullptr, nullptr, nullptr);
}

void RecordCodec::InitFilters()
{

    // allocate filter frame (where the filtered frame will be stored)
    filter_frame_ = av_frame_alloc();

    // create buffer source and sink
    const AVFilter *bufferSrc = avfilter_get_by_name("buffer");
    const AVFilter *bufferSink = avfilter_get_by_name("buffersink");

    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();

    // allocate filter graph
    filter_fraph_ = avfilter_graph_alloc();

    char args[128];
    snprintf(args, sizeof(args), "width=%d:height=%d:pix_fmt=%d:time_base=%d/%d:sar=%d/%d:frame_rate=%d/%d", (int)frame_width_, (int)frame_height_, raw_pix_fmt_, in_ctx_.videoStream->time_base.num, in_ctx_.videoStream->time_base.den, in_ctx_.videoStream->sample_aspect_ratio.num, in_ctx_.videoStream->sample_aspect_ratio.den, frame_rate_.num, frame_rate_.den);
    printf("%s\n", args);

    // create buffer source with the specified params
    auto status = avfilter_graph_create_filter(&buffer_src_ctx_, bufferSrc, "in", args, nullptr, filter_fraph_);
    assert(status >= 0);

    // create buffer sink
    status = avfilter_graph_create_filter(&buffer_sink_ctx_, bufferSink, "out", nullptr, nullptr, filter_fraph_);
    assert(status >= 0);

    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffer_src_ctx_;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffer_sink_ctx_;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    // create filter query
    char frameStepFilterQuery[16];

    if (this->filter_query_.empty())
    {

        snprintf(frameStepFilterQuery, sizeof(frameStepFilterQuery), "fps=fps=%d/%d", static_cast<int>(15), 1);

        // add graph represented by the filter query
        status = avfilter_graph_parse(filter_fraph_, frameStepFilterQuery, inputs, outputs, nullptr);
        assert(status >= 0);
    }
    else
    {

        status = avfilter_graph_parse(filter_fraph_, filter_query_.c_str(), inputs, outputs, nullptr);
        assert(status >= 0);
    }

    status = avfilter_graph_config(filter_fraph_, nullptr);
    assert(status >= 0);
}

int RecordCodec::DecodePacketToFrame(AVCodecContext *codecContext, AVFrame *frame, AVPacket *packet)
{

    int statCode = avcodec_send_packet(codecContext, packet);

    if (statCode < 0)
    {
        return statCode;
    }

    statCode = avcodec_receive_frame(codecContext, frame);

    if (statCode == AVERROR(EAGAIN) || statCode == AVERROR_EOF)
    {
        return statCode;
    }

    if (statCode < 0)
    {
        return statCode;
    }

    return true;
}

int RecordCodec::EncodeFrameToPacket(AVCodecContext *codecContext, AVFrame *frame, AVPacket *packet)
{

    int statCode = avcodec_send_frame(codecContext, frame);
    if (statCode < 0)
    {
        return statCode;
    }

    statCode = avcodec_receive_packet(codecContext, packet);

    if (statCode == AVERROR(EAGAIN) || statCode == AVERROR_EOF)
    {
        return statCode;
    }

    if (statCode < 0)
    {
        return statCode;
    }

    return statCode;
}

void RecordCodec::CleanUp()
{

    avfilter_graph_free(&filter_fraph_);
    avio_close(out_ctx_.formatContext->pb);
    sws_freeContext(converter_ctx_);
    av_packet_free(&decoding_packet_);
    av_packet_free(&encoding_packet_);
    av_frame_free(&raw_frame_);
    av_frame_free(&converted_frame_);
    av_frame_free(&filter_frame_);
    avcodec_free_context(&in_ctx_.codecContext);
    avcodec_free_context(&out_ctx_.codecContext);
    avformat_close_input(&in_ctx_.formatContext);
    avformat_free_context(in_ctx_.formatContext);
    avformat_free_context(out_ctx_.formatContext);

    std::cout << "Cleanup transcoder!" << std::endl;
}

void RecordCodec::SetOnEncodedDataCallback(std::function<void(std::vector<uint8_t> &&)> callback)
{
    encode_cb_ = std::move(callback);
}

const bool RecordCodec::Running() const
{
    return running_flag_.load();
}

std::string RecordCodec::Name() const
{
    return name_;
}

std::string RecordCodec::RtspUrl() const
{
    return url_;
}
