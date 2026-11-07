//
//  RoomCommandController.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/Room/RoomCommandController.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include "Stream/Room/MessageCodec.hpp"

namespace xmax::detail {

RoomCommandController::RoomCommandController(
    Send send,
    Relay relay,
    std::string user,
    std::string bot,
    Now now
)
    : send_(std::move(send)),
      relay_(std::move(relay)),
      now_(std::move(now)),
      user_(std::move(user)),
      bot_(std::move(bot)),
      worker_([this] { run(); }) {
}

RoomCommandController::~RoomCommandController() {
    close();
}

RoomCommandController::Ticket RoomCommandController::submit(
    json message,
    std::string task,
    Cancel cancelled
) {
    require(
        !cancelled(),
        XMAX_CANCELLED,
        "RTC command cancelled"
    );
    auto command = std::make_shared<Command>();
    command->xid = random_id();
    command->kind = message.at("event").get<std::string>();
    require(
        command->kind == "start" || command->kind == "change_condition" ||
            command->kind == "change_target_size",
        XMAX_INVALID_ARGUMENT,
        "Unsupported reliable RTC command"
    );

    message["xid"] = command->xid;
    // Validate before superseding valid pending work or starting the redundant HTTP copy.
    (void)encode_message(
        message.dump(),
        command->xid
    );
    command->message = std::move(message);
    command->task = std::move(task);
    command->validity = std::move(cancelled);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        require(
            !closed_,
            XMAX_CANCELLED,
            "RTC command room is closed"
        );
        for (const auto& previous : commands_)
            if (command->kind == "start" ||
                (previous->task == command->task && previous->kind == command->kind))
                cancel_locked(previous);
        // Cancelled native HTTP phases remain owned until their transport returns.
        require(
            commands_.size() < 64,
            XMAX_RTC_ERROR,
            "RTC command queue is full while cancelled work drains"
        );
        commands_.push_back(command);
    }

    wake_.notify_one();
    return {command->xid, command->result};
}

void RoomCommandController::stop_retries_locked(
    Command& command
) {
    command.retrying = false;
    for (const auto& attempt : command.attempts)
        if (!attempt->initial || command.sent)
            *attempt->cancelled = true;
}

void RoomCommandController::cancel_locked(
    const std::shared_ptr<Command>& command,
    std::exception_ptr error
) {
    *command->cancelled = true;
    stop_retries_locked(*command);
    for (const auto& attempt : command->attempts)
        *attempt->cancelled = true;

    if (!command->settled) {
        command->settled = true;
        command->completion.set_exception(
            error ? error : std::make_exception_ptr(Error(
                XMAX_CANCELLED,
                "RTC command cancelled"
            ))
        );
    }
}

void RoomCommandController::cancel(
    const std::string& task
) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& command : commands_)
        if (task.empty() || task == command->task)
            cancel_locked(command);
    wake_.notify_one();
}

void RoomCommandController::cancel_xid(
    const std::string& xid
) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& command : commands_)
        if (xid == command->xid)
            cancel_locked(command);
    wake_.notify_one();
}

void RoomCommandController::receive(
    const std::string& sender,
    const std::string& data
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || sender == user_ || (!bot_.empty() && sender != bot_))
        return;

    const auto message = decoder_.decode(
        sender,
        data
    );
    if (!message || !message->contains("event") || (*message)["event"] != "xid_ack" ||
        !message->contains("xid") || !(*message)["xid"].is_string() || !message->contains("count") ||
        !(*message)["count"].is_number_integer() || (*message)["count"] < 1 ||
        (*message)["count"] > 9007199254740991LL)
        return;
    if (message->contains("user_id") && !(*message)["user_id"].is_null() &&
        (!(*message)["user_id"].is_string() || (*message)["user_id"] != user_))
        return;

    for (const auto& command : commands_) {
        if (command->xid != (*message)["xid"] || command->next == 0 || !command->retrying ||
            command->cancelled->load())
            continue;
        if (command->validity()) {
            cancel_locked(command);
            return;
        }
        stop_retries_locked(*command);
        XmaxLogger::write(
            LogCategory::Room,
            XMAX_LOG_INFO,
            [&](auto) {
                return "RTC command acknowledged: event=" + command->kind + ", xid=" + command->xid +
                       ", attempts=" + std::to_string(command->next);
            }
        );
    }
    wake_.notify_one();
}

void RoomCommandController::close() {
    std::lock_guard<std::mutex> closing(close_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        for (const auto& command : commands_)
            cancel_locked(command);
    }
    wake_.notify_one();
    if (worker_.joinable())
        worker_.join();
}
} // namespace xmax::detail
