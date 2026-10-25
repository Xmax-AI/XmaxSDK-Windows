//
//  XmaxLogger.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include <optional>

namespace xmax::detail {

/**
 * The SDK subsystem category for diagnostics.
 */
enum class LogCategory {

    /**
     * Realtime session lifecycle diagnostics.
     */
    Realtime,
    /**
     * Native RTC connection, codec, and transport diagnostics.
     */
    RTC,
    /**
     * Capture, input-binding, and frame-processing diagnostics.
     */
    Media,
    /**
     * Service request and response diagnostics.
     */
    API,
    /**
     * File upload, download, and safety-check diagnostics.
     */
    Storage,
    /**
     * Room signaling and keepalive diagnostics.
     */
    Room,
    /**
     * Video publication and quality-policy diagnostics.
     */
    Stream,
    /**
     * Local preview and remote result-delivery diagnostics.
     */
    Render,
    /**
     * Interaction-control diagnostics.
     */
    Interaction,
    /**
     * Device and system-permission diagnostics.
     */
    Permission
};

/**
 * The logging options, output language, and configuration revision.
 */
struct LogConfiguration {

    /**
     * The business and performance logging bitmask.
     */
    uint32_t options;

    /**
     * The log output language.
     */
    xmax_log_language language;

    /**
     * The configuration revision used to discard records from earlier configurations.
     */
    uint64_t generation;
};

/**
 * Provides process-wide business and performance logging, disabled by default, with sensitive content
 * redacted.
 */
class XmaxLogger {

  public:

    /**
     * Replaces process-wide logging settings and clears records pending under the previous configuration.
     *
     * @param[in] options The business and performance logging bitmask; 0 disables logging.
     * @param[in] language The logging language: XMAX_LOG_CHINESE or XMAX_LOG_ENGLISH.
     */
    static void configure(
        uint32_t options,
        xmax_log_language language
    );

    /**
     * Returns a thread-safe snapshot of logging options, language, and configuration revision.
     */
    static LogConfiguration configuration();

    /**
     * Reports whether any requested logging option is enabled.
     *
     * @param[in] options The business and performance logging bitmask; 0 disables logging.
     */
    static bool is_enabled(
        uint32_t options
    ) noexcept;

    /**
     * Removes and returns the oldest log record without blocking.
     *
     * @param[out] record Receives the oldest redacted record; unchanged if the queue is empty.
     *
     * @return True if a record was retrieved; false if the queue is empty.
     */
    static bool poll(
        xmax_log_record& record
    );

    /**
     * Selects text using the captured output language.
     *
     * @param[in] language The logging language: XMAX_LOG_CHINESE or XMAX_LOG_ENGLISH.
     * @param[in] chinese The text used for Chinese output.
     * @param[in] english The text used for English output.
     */
    static std::string localized(
        xmax_log_language language,
        const char* chinese,
        const char* english
    ) {
        return language == XMAX_LOG_CHINESE ? chinese : english;
    }

    /**
     * Registers a secret for redaction for the lifetime of the returned shared guard.
     *
     * @param[in] secret The credential to hide from logs while the returned guard remains alive.
     *
     * @return A shared registration guard; releasing its last reference unregisters the credential. Empty
     * credentials return an empty pointer.
     */
    static std::shared_ptr<const std::string> protect_secret(
        std::string secret
    );

    /**
     * Redacts credential patterns and registered secrets, merging overlapping matches.
     *
     * @param[in] text The UTF-8 content to redact.
     */
    static std::string redact(
        std::string text
    );

    /**
     * Removes URL query parameters and fragments for diagnostics.
     *
     * @param[in] path The URL or request path; queries and fragments are removed and credentials redacted.
     */
    static std::string safe_path(
        const std::string& path
    );

    /**
     * Extracts allowlisted error information, limits its length, and redacts secrets.
     *
     * @param[in] body The service error response; JSON is supported, while other formats produce only an
     * omission notice.
     */
    static std::string failure_body(
        const std::string& body
    );

    /**
     * Normalizes control characters and prefixes each physical log line with its category.
     *
     * @param[in] category The SDK subsystem category for this log message.
     * @param[in] message The redacted UTF-8 message; this method only normalizes controls and adds category
     * prefixes.
     */
    static std::string formatted_message(
        LogCategory category,
        const std::string& message
    );

    /**
     * Lazily formats enabled log messages without interrupting the operation on diagnostic failure.
     *
     * @tparam F The callable type for the operation.
     *
     * @param[in] category The SDK subsystem category for this log message.
     * @param[in] level The severity of the current log record.
     * @param[in] message The UTF-8 formatter, receiving the configured language and invoked only when the
     * logging option is enabled.
     * @param[in] options The business and performance logging bitmask; 0 disables logging.
     */
    template <class F>
    static void write(
        LogCategory category,
        xmax_log_level level,
        F&& message,
        uint32_t options = XMAX_LOG_BUSINESS
    ) noexcept {
        try {
            const auto config = enabled_configuration(options);
            if (config)
                emit(
                    category,
                    level,
                    options,
                    message(config->language),
                    *config
                );
        } catch (...) {
            // Contain formatting, allocation, and system-log failures so media and network operations continue.
        }
    }

  private:

    /**
     * Returns a configuration snapshot only when the requested logging category is enabled.
     *
     * @param[in] options The business and performance logging bitmask; 0 disables logging.
     *
     * @return The configuration if the requested logging option is enabled; otherwise std::nullopt.
     */
    static std::optional<LogConfiguration> enabled_configuration(
        uint32_t options
    );

    /**
     * Writes bounded UTF-8 records, discarding records whose configuration revision is stale.
     *
     * @param[in] category The SDK subsystem category for this log message.
     * @param[in] level The severity of the current log record.
     * @param[in] options The business and performance logging bitmask; 0 disables logging.
     * @param[in] message The UTF-8 message, processed by centralized formatting and redaction.
     * @param[in] config The logging configuration captured before formatting, used to reject stale records.
     */
    static void emit(
        LogCategory category,
        xmax_log_level level,
        uint32_t options,
        std::string message,
        const LogConfiguration& config
    );
};
} // namespace xmax::detail
