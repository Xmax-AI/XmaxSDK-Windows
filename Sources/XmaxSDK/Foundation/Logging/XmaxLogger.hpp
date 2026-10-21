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
 * SDK 诊断所属的子系统分类。
 */
enum class LogCategory {

    /**
     * 实时会话生命周期诊断。
     */
    Realtime,
    /**
     * 原生 RTC 连接、编解码和传输诊断。
     */
    RTC,
    /**
     * 采集、输入绑定和帧处理诊断。
     */
    Media,
    /**
     * 服务请求及响应诊断。
     */
    API,
    /**
     * 文件上传、下载及安全校验诊断。
     */
    Storage,
    /**
     * 房间信令和保活诊断。
     */
    Room,
    /**
     * 视频发布和画质策略诊断。
     */
    Stream,
    /**
     * 本地预览及远端结果交付诊断。
     */
    Render,
    /**
     * 交互控制诊断分类。
     */
    Interaction,
    /**
     * 设备与系统访问权限诊断。
     */
    Permission
};

/**
 * 日志配置，包含日志选项、输出语言和配置版本。
 */
struct LogConfiguration {

    /**
     * 业务与性能日志选项位掩码。
     */
    uint32_t options;

    /**
     * 日志输出语言。
     */
    xmax_log_language language;

    /**
     * 配置版本，用于丢弃旧配置下产生的日志。
     */
    uint64_t generation;
};

/**
 * 提供默认关闭的进程级业务与性能日志，并在输出前清理敏感信息。
 */
class XmaxLogger {

  public:

    /**
     * 替换进程级日志配置，并清空旧配置下等待消费的记录。
     *
     * @param[in] options 业务与性能日志选项位掩码，0 表示关闭日志。
     * @param[in] language 日志输出语言，使用 XMAX_LOG_CHINESE 或 XMAX_LOG_ENGLISH。
     */
    static void configure(
        uint32_t options,
        xmax_log_language language
    );

    /**
     * 线程安全地读取当前日志选项、输出语言和配置版本。
     */
    static LogConfiguration configuration();

    /**
     * 检查指定日志选项中是否至少有一项已启用。
     *
     * @param[in] options 业务与性能日志选项位掩码，0 表示关闭日志。
     */
    static bool is_enabled(
        uint32_t options
    ) noexcept;

    /**
     * 非阻塞读取并移除最早的一条日志。
     *
     * @param[out] record 接收队列中最早的已脱敏日志记录；队列为空时保持原值。
     * @return 取得记录返回 true；队列为空返回 false。
     */
    static bool poll(
        xmax_log_record& record
    );

    /**
     * 根据已捕获的输出语言选择对应文本。
     *
     * @param[in] language 日志输出语言，使用 XMAX_LOG_CHINESE 或 XMAX_LOG_ENGLISH。
     * @param[in] chinese 使用中文输出时选择的文本。
     * @param[in] english 使用英文输出时选择的文本。
     */
    static std::string localized(
        xmax_log_language language,
        const char* chinese,
        const char* english
    ) {
        return language == XMAX_LOG_CHINESE ? chinese : english;
    }

    /**
     * 注册需要脱敏的凭证，并用返回的共享对象维持注册有效期。
     *
     * @param[in] secret 需要从日志中隐藏的凭证，注册有效期与返回的保护对象一致。
     * @return 用于维持脱敏注册的共享对象；最后一个引用释放后，该凭证的注册失效。空凭证返回空指针。
     */
    static std::shared_ptr<const std::string> protect_secret(
        std::string secret
    );

    /**
     * 清理凭证模式与已注册的敏感文本，合并重叠匹配区间。
     *
     * @param[in] text 需要清理凭证模式和注册敏感文本的 UTF-8 内容。
     */
    static std::string redact(
        std::string text
    );

    /**
     * 去除 URL 查询参数和片段，供诊断输出使用。
     *
     * @param[in] path 待移除查询参数和片段的 URL 或请求路径，返回前执行凭证脱敏。
     */
    static std::string safe_path(
        const std::string& path
    );

    /**
     * 按白名单提取错误信息，限制长度并清理敏感内容。
     *
     * @param[in] body 服务端返回的错误响应文本；支持 JSON，其他格式仅保留省略提示。
     */
    static std::string failure_body(
        const std::string& body
    );

    /**
     * 规范化控制字符，并为每个物理文本行添加日志分类前缀。
     *
     * @param[in] category 当前消息所属的 SDK 子系统日志分类。
     * @param[in] message 已脱敏的 UTF-8 消息，本方法仅规范化控制字符和添加分类前缀。
     */
    static std::string formatted_message(
        LogCategory category,
        const std::string& message
    );

    /**
     * 延迟格式化已启用的日志消息，诊断失败不会中断调用操作。
     *
     * @tparam F 待执行操作的可调用对象类型。
     * @param[in] category 当前消息所属的 SDK 子系统日志分类。
     * @param[in] level 当前记录的日志严重程度。
     * @param[in] message 延迟生成 UTF-8 文本的回调，接收当前配置语言，仅在日志选项已启用时调用。
     * @param[in] options 业务与性能日志选项位掩码，0 表示关闭日志。
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
                emit(category, level, options, message(config->language), *config);
        } catch (...) {
            // 隔离格式化、内存分配和系统日志异常，保证媒体与网络操作继续执行。
        }
    }

  private:

    /**
     * 仅在指定日志类别已启用时返回配置快照。
     *
     * @param[in] options 业务与性能日志选项位掩码，0 表示关闭日志。
     * @return 已启用指定日志选项时返回配置快照，否则返回 std::nullopt。
     */
    static std::optional<LogConfiguration> enabled_configuration(
        uint32_t options
    );

    /**
     * 写入 UTF-8 日志并限制记录长度；配置版本已失效时丢弃该记录。
     *
     * @param[in] category 当前消息所属的 SDK 子系统日志分类。
     * @param[in] level 当前记录的日志严重程度。
     * @param[in] options 业务与性能日志选项位掩码，0 表示关闭日志。
     * @param[in] message 待输出的 UTF-8 文本，由统一格式化与脱敏阶段处理。
     * @param[in] config 开始格式化时捕获的日志配置，用于丢弃旧配置下产生的记录。
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
