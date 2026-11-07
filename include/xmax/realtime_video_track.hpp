//
//  realtime_video_track.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "camera_device.hpp"
#include "error.hpp"
#include "handle_owner.hpp"
#include <memory>
#include <mutex>
#include <optional>

namespace xmax {

class realtime;
class realtime_media_stream;

/** Identifies a local or result video track and reads its current negotiated metadata. */
class realtime_video_track {
  public:

    /** Returns a stable identifier within the issuing controller. */
    std::string id() const { return (local_ ? "local-" : "remote-") + std::to_string(revision_); }

    /** Returns the negotiated format, or an empty value for a retired track. */
    std::optional<xmax_video_format> video_format() const {
        xmax_video_format format{};
        if (read(format, nullptr, nullptr))
            return format;
        return {};
    }

    /** Returns the current encoder overrides, or an empty value for a retired track. */
    std::optional<xmax_encoding_config> encoding() const {
        xmax_video_format format{};
        xmax_encoding_config value{};
        value.struct_size = sizeof(value);
        if (read(format, &value, nullptr))
            return value;
        return {};
    }

    /** Returns copied metadata for the camera currently selected by this track. */
    std::optional<camera_device> device() const {
        xmax_video_format format{};
        xmax_camera_list snapshot = nullptr;
        if (!local_ || !read(format, nullptr, &snapshot) || !snapshot)
            return {};
        detail::owner<xmax_camera_list, xmax_camera_list_release> cameras(snapshot);
        xmax_camera_info info{};
        info.struct_size = sizeof(info);
        xmax_camera_position position{};
        check(xmax_camera_list_get(cameras.get(), 0, &info));
        check(xmax_camera_list_get_position(cameras.get(), 0, &position));
        return camera_device{info.id, info.name, info.is_default != 0, position};
    }

    /** Returns the current camera position, or an empty value for a track without a camera. */
    std::optional<xmax_camera_position> position() const {
        if (const auto selected = device())
            return selected->position;
        return {};
    }

  private:

    friend class realtime;
    friend class realtime_media_stream;

    /** Protects a borrowed controller handle until a metadata read finishes. */
    struct binding {
        /** Borrows the handle while its realtime owner remains active. */
        explicit binding(xmax_realtime value) : handle(value) {}
        /** Synchronizes reads with retirement of the controller handle. */
        std::mutex mutex;
        /** The controller handle, cleared before it is destroyed. */
        xmax_realtime handle;
    };

    /** Associates the track with an issuing controller and its current media revision. */
    realtime_video_track(const std::shared_ptr<binding>& owner, uint64_t revision, bool local)
        : owner_(owner), revision_(revision), local_(local) {}

    /** Reads metadata while the owning controller is still available. */
    bool read(xmax_video_format& format, xmax_encoding_config* encoding, xmax_camera_list* camera) const {
        const auto owner = owner_.lock();
        if (!owner)
            return false;
        std::lock_guard<std::mutex> lock(owner->mutex);
        if (!owner->handle)
            return false;
        const auto result = xmax_realtime_get_video_track_info(owner->handle, local_, revision_, &format, encoding, camera);
        check(result);
        return result == XMAX_OK;
    }

    /** The controller binding, which a track does not retain. */
    std::weak_ptr<binding> owner_;
    /** The local media or remote connection revision. */
    uint64_t revision_;
    /** Whether the track describes local media. */
    bool local_;
};

} // namespace xmax
