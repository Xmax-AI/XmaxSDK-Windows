#include "Media/External/ExternalVideoController.hpp"
namespace xmax::detail {
void ExternalVideoController::prepare(xmax_video_format format) {
    validate_format(format);
    require(format.width >= 4 && format.height >= 4, XMAX_INVALID_ARGUMENT,
            "External input must be at least 4 by 4 pixels");
    std::lock_guard<std::mutex> lock(mutex_);
    require(!requesting_, XMAX_INVALID_STATE, "Stop delivery before replacing an external source");
    format_ = format;
    last_timestamp_ = -1;
}
void ExternalVideoController::start(VideoFrameSink sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    require(format_.width != 0, XMAX_INVALID_STATE, "No external input is bound");
    require(bool(sink), XMAX_INVALID_ARGUMENT, "An external frame sink is required");
    sink_ = std::move(sink);
    requesting_ = true;
}
void ExternalVideoController::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    requesting_ = false;
    sink_ = {};
}
void ExternalVideoController::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    requesting_ = false;
    sink_ = {};
    format_ = {};
    last_timestamp_ = -1;
}
bool ExternalVideoController::bound() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return format_.width != 0;
}
xmax_video_format ExternalVideoController::format() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return format_;
}
void ExternalVideoController::push(const xmax_video_frame& source) {
    // The lock also drains in-flight delivery before stop() returns.
    std::lock_guard<std::mutex> lock(mutex_);
    require(format_.width != 0, XMAX_INVALID_STATE, "No external input is bound");
    require(source.width == format_.width && source.height == format_.height, XMAX_INVALID_ARGUMENT,
            "External frame does not match the negotiated format");
    require(source.timestamp_us > last_timestamp_, XMAX_INVALID_ARGUMENT,
            "External frame timestamps must increase strictly");
    auto frame = Frame::copy(source);
    last_timestamp_ = source.timestamp_us;
    if (requesting_ && sink_)
        sink_(frame);
    if (preview_)
        preview_(std::move(frame));
}
void ExternalVideoController::preview(std::function<void(Frame)> sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    preview_ = std::move(sink);
}
void ExternalVideoController::push_owned(Frame frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    require(format_.width && frame.width == format_.width && frame.height == format_.height,
            XMAX_INVALID_STATE, "Captured frame does not match the bound source");
    require(frame.timestamp_us > last_timestamp_, XMAX_INVALID_ARGUMENT, "Capture timestamps must increase");
    last_timestamp_ = frame.timestamp_us;
    if (requesting_ && sink_)
        sink_(frame);
    if (preview_)
        preview_(std::move(frame));
}
} // namespace xmax::detail
