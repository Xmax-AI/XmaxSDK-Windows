//
//  XmaxLogger.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Logging/XmaxLogger.hpp"
#include "Foundation/Json.hpp"
#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>
#include <regex>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <os/log.h>
#else
#include <cstdio>
#endif

namespace xmax::detail {
namespace {

/**
 * Retains process-wide logging configuration, a bounded record queue, and credential references for
 * redaction.
 */
struct State {

    /**
     * The mutex protecting log configuration, records, and weak credential registrations.
     */
    std::mutex mutex;

    /**
     * The current logging options, output language, and configuration revision.
     */
    LogConfiguration config{XMAX_LOG_NONE, XMAX_LOG_CHINESE, 0};

    /**
     * A process-wide increasing sequence number; dropped records can leave gaps.
     */
    uint64_t sequence = 0;

    /**
     * The bounded, ordered log queue for a single consumer.
     */
    std::deque<xmax_log_record> records;

    /**
     * Weak references to registered credentials; they do not extend guard lifetimes.
     */
    std::vector<std::weak_ptr<const std::string>> secrets;
};

/**
 * Returns process-wide logging state whose mutable fields require the state mutex.
 *
 * @return The process-wide logging state; hold its mutex before accessing mutable fields.
 */
State& state() {
    static State value;
    return value;
}

/**
 * Converts an internal logging category to a stable output label.
 *
 * @param[in] category The SDK subsystem category for this log message.
 *
 * @return The static category label; do not modify or release it.
 */
const char* category_name(
    LogCategory category
) {
    switch (category) {
    case LogCategory::Realtime:
        return "Realtime";
    case LogCategory::RTC:
        return "RTC";
    case LogCategory::Media:
        return "Media";
    case LogCategory::API:
        return "API";
    case LogCategory::Storage:
        return "Storage";
    case LogCategory::Room:
        return "Room";
    case LogCategory::Stream:
        return "Stream";
    case LogCategory::Render:
        return "Render";
    case LogCategory::Interaction:
        return "Interaction";
    case LogCategory::Permission:
        return "Permission";
    }
    return "Realtime";
}

/**
 * Normalizes sensitive field names to lowercase ASCII alphanumeric characters.
 *
 * @param[in] value The sensitive field name to normalize.
 */
std::string normalized(
    std::string value
) {
    std::string key;
    for (unsigned char c : value)
        if (c >= 'A' && c <= 'Z')
            key += char(c + ('a' - 'A'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            key += char(c);
    return key;
}

/**
 * Identifies credentials, prompts, reference media, and nested success-payload fields.
 *
 * @param[in] value The JSON field name to classify.
 */
bool sensitive_key(
    const std::string& value
) {
    const auto key = normalized(value);
    for (const auto* word :
         {"apikey",
          "token",
          "secret",
          "authorization",
          "credential",
          "password",
          "accesskey",
          "signature",
          "cookie",
          "prompt",
          "referencepath"})
        if (key.find(word) != std::string::npos)
            return true;
    return key == "data"; // Never dump a session/STS success payload embedded in a failure envelope.
}

/**
 * Redacts sensitive fields and nested JSON text with bounded recursion.
 *
 * @param[in,out] value The JSON value to redact recursively.
 * @param[in] depth The current JSON recursion depth, used to bound nested parsing.
 */
void scrub_json(
    json& value,
    int depth = 0
) {
    if (depth > 16) {
        value = "[omitted]";
        return;
    }

    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it)
            if (sensitive_key(it.key()))
                it.value() = "[redacted]";
            else
                scrub_json(
                    it.value(),
                    depth + 1
                );
    } else if (value.is_array()) {
        for (auto& child : value)
            scrub_json(
                child,
                depth + 1
            );
    } else if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        if (!text.empty() && (text.front() == '{' || text.front() == '[')) {
            auto nested = json::parse(
                text,
                nullptr,
                false
            );
            if (!nested.is_discarded()) {
                scrub_json(
                    nested,
                    depth + 1
                );
                value = nested.dump();
            }
        }
    }
}

/**
 * Truncates UTF-8 text to a byte budget without splitting characters.
 *
 * @param[in] value The UTF-8 text to truncate.
 * @param[in] count The maximum retained bytes; incomplete characters are discarded in full.
 */
std::string utf8_prefix(
    const std::string& value,
    size_t count
) {
    count = std::min(count, value.size());
    while (count && count < value.size() && (static_cast<unsigned char>(value[count]) & 0xc0) == 0x80)
        --count;
    return value.substr(
        0,
        count
    );
}

/**
 * Copies a complete UTF-8 prefix into a nonempty fixed buffer and adds NUL termination.
 *
 * @param[out] output The writable destination with capacity accessible bytes.
 * @param[in] capacity The total output capacity in bytes, including the trailing NUL.
 * @param[in] text The UTF-8 text to copy, preserving character boundaries when truncated.
 */
void copy_text(
    char* output,
    size_t capacity,
    const std::string& text
) {
    const auto clipped = utf8_prefix(
        text,
        capacity - 1
    );
    std::memcpy(
        output,
        clipped.data(),
        clipped.size()
    );
    output[clipped.size()] = 0;
}

/**
 * Writes formatted records to platform diagnostics without propagating logging exceptions to calling code.
 *
 * @param[in] record The formatted, redacted record sent to system diagnostics at its severity.
 */
void platform_write(
    const xmax_log_record& record
) noexcept {
#ifdef _WIN32
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, record.message, -1, nullptr, 0);
    if (size <= 0)
        return;
    try {
        std::wstring text(size_t(size), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, record.message, -1, text.data(), size)) {
            text.resize(size_t(size - 1));
            static const wchar_t* levels[]{L"[debug] ", L"[info] ", L"[warning] ", L"[error] "};
            text.insert(0, levels[record.level]);
            text += L"\n";
            OutputDebugStringW(text.c_str());
        }
    } catch (...) {
    }
#elif defined(__APPLE__)
    static os_log_t log = os_log_create(
        "ai.xmax.XmaxSDK",
        "XmaxSDK"
    );
    const os_log_type_t levels[]{OS_LOG_TYPE_DEBUG, OS_LOG_TYPE_INFO, OS_LOG_TYPE_DEFAULT, OS_LOG_TYPE_ERROR};
    os_log_with_type(log, levels[record.level], "%{public}s", record.message);
#else
    std::fprintf(stderr, "%s\n", record.message);
#endif
}
} // namespace

/**
 * Replaces process-wide logging settings and clears records pending under the previous configuration.
 *
 * @param[in] options The business and performance logging bitmask; 0 disables logging.
 * @param[in] language The logging language: XMAX_LOG_CHINESE or XMAX_LOG_ENGLISH.
 */
void XmaxLogger::configure(
    uint32_t options,
    xmax_log_language language
) {
    require(
        (options & ~uint32_t(XMAX_LOG_ALL)) == 0,
        XMAX_INVALID_ARGUMENT,
        "Invalid logger options"
    );
    require(
        language == XMAX_LOG_CHINESE || language == XMAX_LOG_ENGLISH,
        XMAX_INVALID_ARGUMENT,
        "Invalid logger language"
    );

    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.config = {options, language, s.config.generation + 1};
    s.records.clear(); // Reconfiguration cannot expose stale entries from a previously enabled mask.
}

/**
 * Returns a thread-safe snapshot of logging options, language, and configuration revision.
 */
LogConfiguration XmaxLogger::configuration() {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.config;
}

/**
 * Returns a configuration snapshot only when the requested logging category is enabled.
 *
 * @param[in] options The business and performance logging bitmask; 0 disables logging.
 *
 * @return The configuration if the requested logging option is enabled; otherwise std::nullopt.
 */
std::optional<LogConfiguration> XmaxLogger::enabled_configuration(
    uint32_t options
) {
    if (!options || (options & ~uint32_t(XMAX_LOG_ALL)))
        return {};
    const auto config = configuration();
    return (config.options & options) == options ? std::optional<LogConfiguration>(config) : std::nullopt;
}

/**
 * Reports whether any requested logging option is enabled.
 *
 * @param[in] options The business and performance logging bitmask; 0 disables logging.
 */
bool XmaxLogger::is_enabled(
    uint32_t options
) noexcept {
    try {
        return bool(enabled_configuration(options));
    } catch (...) {
        return false;
    }
}

/**
 * Registers a secret for redaction for the lifetime of the returned shared guard.
 *
 * @param[in] secret The credential to hide from logs while the returned guard remains alive.
 *
 * @return A shared registration guard; releasing its last reference unregisters the credential. Empty
 * credentials return an empty pointer.
 */
std::shared_ptr<const std::string> XmaxLogger::protect_secret(
    std::string secret
) {
    if (secret.empty())
        return {};

    auto guard = std::make_shared<const std::string>(std::move(secret));
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.secrets.erase(
        std::remove_if(
            s.secrets.begin(),
            s.secrets.end(),
            [](const auto& p) { return p.expired(); }
        ),
        s.secrets.end()
    );
    s.secrets.push_back(guard);

    return guard;
}

/**
 * Redacts credential patterns and registered secrets, merging overlapping matches.
 *
 * @param[in] text The UTF-8 content to redact.
 */
std::string XmaxLogger::redact(
    std::string text
) {
    // Repair invalid UTF-8 before truncating text for delivery.
    text = json::parse(json(text).dump(
        -1,
        ' ',
        false,
        json::error_handler_t::replace
    ))
               .get<std::string>();

    std::vector<std::shared_ptr<const std::string>> secrets;
    {
        auto& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        for (const auto& weak : s.secrets)
            if (auto secret = weak.lock())
                secrets.push_back(std::move(secret));
    }

    // Merge sensitive spans before redaction so replacements cannot damage adjacent credential matches.
    std::vector<std::pair<size_t, size_t>> ranges;
    const auto collect = [&](const std::regex& expression, size_t group) {
        for (auto it = std::sregex_iterator(
            text.begin(),
            text.end(),
            expression
        );
             it != std::sregex_iterator();
             ++it) {
            const auto begin = size_t(it->position(group));
            ranges.emplace_back(
                begin,
                begin + size_t(it->length(group))
            );
        }
    };
    static const std::regex fields(
        R"((\b(?:x[-_])?(?:api[-_]?key|(?:[a-z]+[-_]?)?token|secret(?:accesskey)?|authorization|password|credential[s]?|accesskeyid|q-signature)\b["']?\s*[:=]\s*)("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[^\r\n,;}]+))",
        std::regex::icase
    );
    static const std::regex bearer(
        R"((\bBearer\s+)([^\s,;"']+))",
        std::regex::icase
    );
    static const std::regex query(
        R"((https?://[^\s?"']+\?)([^\s"']*))",
        std::regex::icase
    );
    collect(fields, 2);
    collect(bearer, 2);
    collect(query, 2);

    for (const auto& secret : secrets) {
        const auto escaped = json(*secret).dump();
        for (const auto& needle : {*secret, escaped.substr(
            1,
            escaped.size() - 2
        )}) {
            size_t position = 0;
            while (!needle.empty() && (position = text.find(
                needle,
                position
            )) != std::string::npos) {
                ranges.emplace_back(
                    position,
                    position + needle.size()
                );
                position += needle.size();
            }
        }
    }

    std::sort(
        ranges.begin(),
        ranges.end()
    );

    std::vector<std::pair<size_t, size_t>> merged;

    for (const auto& range : ranges) {
        if (range.first == range.second)
            continue;
        if (!merged.empty() && range.first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, range.second);
        else
            merged.push_back(range);
    }

    std::string result;
    size_t position = 0;

    for (const auto& range : merged) {
        result.append(
            text,
            position,
            range.first - position
        );
        result += "[redacted]";
        position = range.second;
    }

    result.append(
        text,
        position,
        std::string::npos
    );

    return result;
}

/**
 * Removes URL query parameters and fragments for diagnostics.
 *
 * @param[in] path The URL or request path; queries and fragments are removed and credentials redacted.
 */
std::string XmaxLogger::safe_path(
    const std::string& path
) {
    return path.substr(
        0,
        path.find_first_of("?#")
    );
}

/**
 * Extracts allowlisted error information, limits its length, and redacts secrets.
 *
 * @param[in] body The service error response; JSON is supported, while other formats produce only an
 * omission notice.
 */
std::string XmaxLogger::failure_body(
    const std::string& body
) {
    if (body.size() > 64 * 1024)
        return "<Response body omitted: exceeds diagnostic limit>";
    try {
        auto value = json::parse(body);

        if (!value.is_object())
            return "<Non-object response body omitted>";

        json diagnostic = json::object();

        for (const auto* name :
             {"success",
              "status",
              "code",
              "errorCode",
              "errcode",
              "message",
              "msg",
              "error",
              "error_message",
              "error_description",
              "reason",
              "detail",
              "details",
              "requestId",
              "traceId"})
            if (value.contains(name))
                diagnostic[name] = value[name];

        scrub_json(diagnostic);

        return utf8_prefix(
            redact(diagnostic.dump(
                2,
                ' ',
                false,
                json::error_handler_t::replace
            )),
            2048
        );
    } catch (...) {
        return "<Non-JSON response body omitted>";
    }
}

/**
 * Normalizes control characters and prefixes each physical log line with its category.
 *
 * @param[in] category The SDK subsystem category for this log message.
 * @param[in] message The redacted UTF-8 message; this method only normalizes controls and adds category
 * prefixes.
 */
std::string XmaxLogger::formatted_message(
    LogCategory category,
    const std::string& message
) {
    const std::string prefix = std::string("[Xmax][") + category_name(category) + "] ";
    std::string output = prefix;

    for (unsigned char c : message) {
        if (c == '\n')
            output += '\n' + prefix;
        else if (c == '\r')
            output += "\\r";
        else if (c == 0 || (c < 32 && c != '\t'))
            output += '?';
        else
            output += char(c);
    }

    return output;
}

/**
 * Writes bounded UTF-8 records, discarding records whose configuration revision is stale.
 *
 * @param[in] category The SDK subsystem category for this log message.
 * @param[in] level The severity of the current log record.
 * @param[in] options The business and performance logging bitmask; 0 disables logging.
 * @param[in] message The UTF-8 message, processed by centralized formatting and redaction.
 * @param[in] config The logging configuration captured before formatting, used to reject stale records.
 */
void XmaxLogger::emit(
    LogCategory category,
    xmax_log_level level,
    uint32_t options,
    std::string message,
    const LogConfiguration& config
) {
    xmax_log_record record{};
    record.struct_size = sizeof(record);
    record.level = level;
    record.options = options;
    record.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              std::chrono::system_clock::now().time_since_epoch()
    )
                              .count();
    copy_text(
        record.category,
        sizeof(record.category),
        category_name(category)
    );

    const auto formatted = formatted_message(
        category,
        redact(std::move(message))
    );
    const auto clipped = formatted.size() >= sizeof(record.message)
                             ? utf8_prefix(
                                 formatted,
                                 sizeof(record.message) - 16
                             ) + " [truncated]"
                             : formatted;
    copy_text(
        record.message,
        sizeof(record.message),
        clipped
    );

    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);

    if (s.config.generation != config.generation)
        return;

    record.sequence = ++s.sequence;

    if (s.records.size() >= 256)
        s.records.pop_front();

    s.records.push_back(record);
    // Share the configuration lock so disabling logs prevents stale system output.
    platform_write(record);
}

/**
 * Removes and returns the oldest log record without blocking.
 *
 * @param[out] record Receives the oldest redacted record; unchanged if the queue is empty.
 *
 * @return True if a record was retrieved; false if the queue is empty.
 */
bool XmaxLogger::poll(
    xmax_log_record& record
) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);

    if (s.records.empty())
        return false;

    record = s.records.front();
    s.records.pop_front();

    return true;
}
} // namespace xmax::detail
