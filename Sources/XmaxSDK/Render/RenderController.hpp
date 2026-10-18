#pragma once
#include "Render/Video/RemoteVideoFramePipeline.hpp"
namespace xmax::detail {
// Display boundary for the C frame API. A native renderer can consume the same owned frames.
// Native Windows CPU presentation lives in Foundation/Render; hosts may present owned frames themselves.
class RenderController {
  public:
    void bind(uint64_t token) { remote_.activate(token); }
    void unbind(uint64_t token) { remote_.deactivate(token); }
    void receive(Frame frame, uint64_t token) { remote_.submit(std::move(frame), token); }
    std::shared_ptr<Frame> acquire_frame() { return remote_.acquire(); }
    void bind_local(uint64_t token) { local_.activate(token); }
    void unbind_local(uint64_t token) { local_.deactivate(token); }
    void receive_local(Frame frame, uint64_t token) { local_.submit(std::move(frame), token); }
    std::shared_ptr<Frame> acquire_local_frame() { return local_.acquire(); }

  private:
    RemoteVideoFramePipeline remote_;
    RemoteVideoFramePipeline local_;
};
} // namespace xmax::detail
