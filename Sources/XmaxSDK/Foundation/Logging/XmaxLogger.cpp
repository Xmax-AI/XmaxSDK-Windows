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
 * 保存进程级日志配置、设有容量上限的记录队列及需要脱敏的凭证引用。
 */
struct State {

    /**
     * 保护日志配置、记录队列和凭证弱引用集合的互斥锁。
     */
    std::mutex mutex;

    /**
     * 当前使用的日志选项、输出语言和配置版本。
     */
    LogConfiguration config{XMAX_LOG_NONE, XMAX_LOG_CHINESE, 0};

    /**
     * 进程内单调递增的记录序号；丢弃记录时可能不连续。
     */
    uint64_t sequence = 0;

    /**
     * 按顺序等待单一消费者读取的设有容量上限的日志队列。
     */
    std::deque<xmax_log_record> records;

    /**
     * 已注册凭证的弱引用集合，不延长凭证保护对象的生命周期。
     */
    std::vector<std::weak_ptr<const std::string>> secrets;
};

/**
 * 获取进程级日志状态，其可变字段由状态互斥锁保护。
 *
 * @return 进程级日志状态的引用，访问可变字段前须持有其互斥锁。
 */
State& state() {
    static State value;
    return value;
}

/**
 * 将内部日志分类转换为稳定的输出标签。
 *
 * @param[in] category 当前消息所属的 SDK 子系统日志分类。
 * @return 静态日志分类标签，不得修改或释放。
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
 * 将敏感字段名规范化为小写 ASCII 字母数字文本。
 *
 * @param[in] value 待规范化的敏感字段名称。
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
 * 识别凭证、提示词、参考媒体及嵌套成功负载字段。
 *
 * @param[in] value 待分类的 JSON 字段名称。
 */
bool sensitive_key(
    const std::string& value
) {
    const auto key = normalized(value);
    for (const auto* word :
         {"apikey", "token", "secret", "authorization", "credential", "password", "accesskey", "signature",
          "cookie", "prompt", "referencepath"})
        if (key.find(word) != std::string::npos)
            return true;
    return key == "data"; // Never dump a session/STS success payload embedded in a failure envelope.
}

/**
 * 清理敏感字段和嵌套 JSON 文本，并限制递归深度。
 *
 * @param[in,out] value 需要递归清理敏感内容的 JSON 值。
 * @param[in] depth 当前 JSON 递归深度，用于限制嵌套解析成本。
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
                scrub_json(it.value(), depth + 1);
    } else if (value.is_array()) {
        for (auto& child : value)
            scrub_json(child, depth + 1);
    } else if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        if (!text.empty() && (text.front() == '{' || text.front() == '[')) {
            auto nested = json::parse(text, nullptr, false);
            if (!nested.is_discarded()) {
                scrub_json(nested, depth + 1);
                value = nested.dump();
            }
        }
    }
}

/**
 * 按字节预算截取 UTF-8 文本，避免拆分字符。
 *
 * @param[in] value 待按字节预算截取的 UTF-8 文本。
 * @param[in] count 最多保留的字节数，不完整字符将整体舍弃。
 */
std::string utf8_prefix(
    const std::string& value,
    size_t count
) {
    count = std::min(count, value.size());
    while (count && count < value.size() && (static_cast<unsigned char>(value[count]) & 0xc0) == 0x80)
        --count;
    return value.substr(0, count);
}

/**
 * 向非空固定缓冲区复制完整 UTF-8 字符前缀并追加 NUL。
 *
 * @param[out] output 可写目标缓冲区，必须具有 capacity 字节的可访问空间。
 * @param[in] capacity 输出缓冲区总字节容量，需包含结尾 NUL 的空间。
 * @param[in] text 需要复制的 UTF-8 文本，截取时保留完整字符边界。
 */
void copy_text(
    char* output,
    size_t capacity,
    const std::string& text
) {
    const auto clipped = utf8_prefix(text, capacity - 1);
    std::memcpy(output, clipped.data(), clipped.size());
    output[clipped.size()] = 0;
}

/**
 * 将规范化记录写入平台诊断输出，不向业务代码传播异常。
 *
 * @param[in] record 已完成格式化和脱敏的日志记录，按其严重程度输出到系统诊断通道。
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
    static os_log_t log = os_log_create("ai.xmax.XmaxSDK", "XmaxSDK");
    const os_log_type_t levels[]{OS_LOG_TYPE_DEBUG, OS_LOG_TYPE_INFO, OS_LOG_TYPE_DEFAULT, OS_LOG_TYPE_ERROR};
    os_log_with_type(log, levels[record.level], "%{public}s", record.message);
#else
    std::fprintf(stderr, "%s\n", record.message);
#endif
}
} // namespace

/**
 * 替换进程级日志配置，并清空旧配置下等待消费的记录。
 *
 * @param[in] options 业务与性能日志选项位掩码，0 表示关闭日志。
 * @param[in] language 日志输出语言，使用 XMAX_LOG_CHINESE 或 XMAX_LOG_ENGLISH。
 */
void XmaxLogger::configure(
    uint32_t options,
    xmax_log_language language
) {
    require((options & ~uint32_t(XMAX_LOG_ALL)) == 0, XMAX_INVALID_ARGUMENT, "Invalid logger options");
    require(
        language == XMAX_LOG_CHINESE || language == XMAX_LOG_ENGLISH, XMAX_INVALID_ARGUMENT,
        "Invalid logger language"
    );

    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.config = {options, language, s.config.generation + 1};
    s.records.clear(); // Reconfiguration cannot expose stale entries from a previously enabled mask.
}

/**
 * 线程安全地读取当前日志选项、输出语言和配置版本。
 */
LogConfiguration XmaxLogger::configuration() {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.config;
}

/**
 * 仅在指定日志类别已启用时返回配置快照。
 *
 * @param[in] options 业务与性能日志选项位掩码，0 表示关闭日志。
 * @return 已启用指定日志选项时返回配置快照，否则返回 std::nullopt。
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
 * 检查指定日志选项中是否至少有一项已启用。
 *
 * @param[in] options 业务与性能日志选项位掩码，0 表示关闭日志。
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
 * 注册需要脱敏的凭证，并用返回的共享对象维持注册有效期。
 *
 * @param[in] secret 需要从日志中隐藏的凭证，注册有效期与返回的保护对象一致。
 * @return 用于维持脱敏注册的共享对象；最后一个引用释放后，该凭证的注册失效。空凭证返回空指针。
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
        std::remove_if(s.secrets.begin(), s.secrets.end(), [](const auto& p) { return p.expired(); }),
        s.secrets.end()
    );
    s.secrets.push_back(guard);

    return guard;
}

/**
 * 清理凭证模式与已注册的敏感文本，合并重叠匹配区间。
 *
 * @param[in] text 需要清理凭证模式和注册敏感文本的 UTF-8 内容。
 */
std::string XmaxLogger::redact(
    std::string text
) {
    // 先修复无效 UTF-8，再截取文本并交付给接入方。
    text = json::parse(json(text).dump(-1, ' ', false, json::error_handler_t::replace)).get<std::string>();

    std::vector<std::shared_ptr<const std::string>> secrets;
    {
        auto& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        for (const auto& weak : s.secrets)
            if (auto secret = weak.lock())
                secrets.push_back(std::move(secret));
    }

    // 在原文中收集敏感区间后合并遮蔽，避免分次替换破坏凭证或相邻字段名。
    std::vector<std::pair<size_t, size_t>> ranges;
    const auto collect = [&](const std::regex& expression, size_t group) {
        for (auto it = std::sregex_iterator(text.begin(), text.end(), expression);
             it != std::sregex_iterator(); ++it) {
            const auto begin = size_t(it->position(group));
            ranges.emplace_back(begin, begin + size_t(it->length(group)));
        }
    };
    static const std::regex fields(
        R"((\b(?:x[-_])?(?:api[-_]?key|(?:[a-z]+[-_]?)?token|secret(?:accesskey)?|authorization|password|credential[s]?|accesskeyid|q-signature)\b["']?\s*[:=]\s*)("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[^\r\n,;}]+))",
        std::regex::icase
    );
    static const std::regex bearer(R"((\bBearer\s+)([^\s,;"']+))", std::regex::icase);
    static const std::regex query(R"((https?://[^\s?"']+\?)([^\s"']*))", std::regex::icase);
    collect(fields, 2);
    collect(bearer, 2);
    collect(query, 2);

    for (const auto& secret : secrets) {
        const auto escaped = json(*secret).dump();
        for (const auto& needle : {*secret, escaped.substr(1, escaped.size() - 2)}) {
            size_t position = 0;
            while (!needle.empty() && (position = text.find(needle, position)) != std::string::npos) {
                ranges.emplace_back(position, position + needle.size());
                position += needle.size();
            }
        }
    }

    std::sort(ranges.begin(), ranges.end());

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
        result.append(text, position, range.first - position);
        result += "[redacted]";
        position = range.second;
    }

    result.append(text, position, std::string::npos);

    return result;
}

/**
 * 去除 URL 查询参数和片段，供诊断输出使用。
 *
 * @param[in] path 待移除查询参数和片段的 URL 或请求路径，返回前执行凭证脱敏。
 */
std::string XmaxLogger::safe_path(
    const std::string& path
) {
    return path.substr(0, path.find_first_of("?#"));
}

/**
 * 按白名单提取错误信息，限制长度并清理敏感内容。
 *
 * @param[in] body 服务端返回的错误响应文本；支持 JSON，其他格式仅保留省略提示。
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
             {"success", "status", "code", "errorCode", "errcode", "message", "msg", "error", "error_message",
              "error_description", "reason", "detail", "details", "requestId", "traceId"})
            if (value.contains(name))
                diagnostic[name] = value[name];

        scrub_json(diagnostic);

        return utf8_prefix(redact(diagnostic.dump(2, ' ', false, json::error_handler_t::replace)), 2048);
    } catch (...) {
        return "<Non-JSON response body omitted>";
    }
}

/**
 * 规范化控制字符，并为每个物理文本行添加日志分类前缀。
 *
 * @param[in] category 当前消息所属的 SDK 子系统日志分类。
 * @param[in] message 已脱敏的 UTF-8 消息，本方法仅规范化控制字符和添加分类前缀。
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
 * 写入 UTF-8 日志并限制记录长度；配置版本已失效时丢弃该记录。
 *
 * @param[in] category 当前消息所属的 SDK 子系统日志分类。
 * @param[in] level 当前记录的日志严重程度。
 * @param[in] options 业务与性能日志选项位掩码，0 表示关闭日志。
 * @param[in] message 待输出的 UTF-8 文本，由统一格式化与脱敏阶段处理。
 * @param[in] config 开始格式化时捕获的日志配置，用于丢弃旧配置下产生的记录。
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
    copy_text(record.category, sizeof(record.category), category_name(category));

    const auto formatted = formatted_message(category, redact(std::move(message)));
    const auto clipped = formatted.size() >= sizeof(record.message)
                             ? utf8_prefix(formatted, sizeof(record.message) - 16) + " [truncated]"
                             : formatted;
    copy_text(record.message, sizeof(record.message), clipped);

    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);

    if (s.config.generation != config.generation)
        return;

    record.sequence = ++s.sequence;

    if (s.records.size() >= 256)
        s.records.pop_front();

    s.records.push_back(record);
    // 与日志配置共用互斥锁，防止关闭日志后仍向系统通道写入旧记录。
    platform_write(record);
}

/**
 * 非阻塞读取并移除最早的一条日志。
 *
 * @param[out] record 接收队列中最早的已脱敏日志记录；队列为空时保持原值。
 * @return 取得记录返回 true；队列为空返回 false。
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
