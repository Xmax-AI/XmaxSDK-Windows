//
//  controller_beauty.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "controller.hpp"
#include <QPointer>
#include <QSettings>
#include <cmath>

QVariantMap XLabController::beautyOptions() const {
    return {{"smoothness", beauty_options_.smoothness},
            {"lightening", beauty_options_.lightening},
            {"sharpness", beauty_options_.sharpness},
            {"redness", beauty_options_.redness}};
}

void XLabController::saveBeautyPreferences() {
    if (preview)
        return;
    QSettings settings;
    settings.setValue("xmax.xlab.beauty.enabled", beauty_enabled_);
    const auto options = beautyOptions();
    for (auto it = options.begin(); it != options.end(); ++it)
        settings.setValue("xmax.xlab.beauty." + it.key(), it.value());
}

void XLabController::setBeautyEnabled(bool enabled) {
    if (closing || beauty_enabled_ == enabled)
        return;
    beauty_enabled_ = enabled;
    ++beauty_revision_;
    beauty_debounce_.start();
    emit changed();
}

void XLabController::setBeautyStrength(const QString& name, float value) {
    if (closing || !beauty_enabled_ || !std::isfinite(value) || value < 0 || value > 1)
        return;
    float* target = name == "smoothness"   ? &beauty_options_.smoothness
                    : name == "lightening" ? &beauty_options_.lightening
                    : name == "sharpness"  ? &beauty_options_.sharpness
                    : name == "redness"    ? &beauty_options_.redness
                                           : nullptr;
    if (!target || *target == value)
        return;
    *target = value;
    ++beauty_revision_;
    beauty_debounce_.start();
    emit changed();
}

void XLabController::registerBeautyListener(quint64 epoch) {
    struct Context {
        QPointer<XLabController> owner;
        quint64 epoch;
    };

    auto context = new Context{this, epoch};
    const xmax_video_beauty_error_listener listener{
        sizeof(listener),
        [](void* user, const xmax_error_info*) {
            const auto context = *static_cast<Context*>(user);
            if (context.owner)
                QMetaObject::invokeMethod(
                    context.owner,
                    [context] {
                        if (context.owner)
                            context.owner->beautyFailed(context.epoch);
                    },
                    Qt::QueuedConnection);
        },
        context, [](void* user) { delete static_cast<Context*>(user); }};
    if (xmax_realtime_set_remote_video_beauty_error_listener(handles_->realtime.get(), &listener) < 0) {
        delete context;
        beautyFailed(epoch);
    }
}

void XLabController::beautyFailed(quint64 epoch) {
    if (epoch != lifecycle_ || closing || !handles_ || !beauty_enabled_)
        return;
    beauty_enabled_ = false;
    ++beauty_revision_;
    saveBeautyPreferences();
    beauty_debounce_.start();
    error = "Beauty is unavailable and has been turned off. Generation will continue.";
    emit changed();
}

void XLabController::applyBeauty() {
    if (preview || !handles_ || !beauty_ready_ || closing || beauty_updating_)
        return;
    beauty_debounce_.stop();
    saveBeautyPreferences();
    beauty_updating_ = true;
    const auto handles = handles_;
    const auto epoch = lifecycle_, revision = beauty_revision_;
    const auto options = beauty_options_;
    const bool enabled = beauty_enabled_;
    emit changed();
    async(
        [handles, options, enabled] {
            const auto result =
                xmax_realtime_set_remote_video_beauty(handles->realtime.get(), enabled ? &options : nullptr);
            // Cancellation during stop/replacement must not change saved effect preferences.
            return result < 0 && result != XMAX_CANCELLED && result != XMAX_INVALID_STATE
                       ? QString::fromUtf8(xmax_last_error())
                       : QString();
        },
        [this, epoch, revision](QString message) {
            if (epoch != lifecycle_)
                return;
            beauty_updating_ = false;
            if (revision != beauty_revision_) {
                applyBeauty();
                return;
            }
            if (!message.isEmpty())
                beautyFailed(epoch);
            emit changed();
        });
}
