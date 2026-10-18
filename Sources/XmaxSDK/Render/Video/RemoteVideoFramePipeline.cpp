#include "Render/Video/RemoteVideoFramePipeline.hpp"
namespace xmax::detail {
void RemoteVideoFramePipeline::activate(uint64_t token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (token < token_)
        return;
    token_ = token;
    latest_.reset();
    active_ = true;
}
void RemoteVideoFramePipeline::deactivate(uint64_t token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (token < token_)
        return;
    token_ = token;
    active_ = false;
    latest_.reset();
}
void RemoteVideoFramePipeline::submit(Frame frame, uint64_t token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ && token == token_)
        latest_ = std::make_shared<Frame>(std::move(frame));
}
std::shared_ptr<Frame> RemoteVideoFramePipeline::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(latest_, {});
}
} // namespace xmax::detail
