//
//  logger.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "error.hpp"
#include <utility>

namespace xmax {

/**
 * 提供进程级日志配置及非阻塞消费接口，不依赖客户端或实时会话实例。
 */
class logger {

  public:

    /**
     * 设置进程级日志选项和输出语言，默认不启用日志。
     *
     * @param[in] options 业务与性能日志选项位掩码，0 表示关闭日志。
     * @param[in] language 日志输出语言，使用 XMAX_LOG_CHINESE 或 XMAX_LOG_ENGLISH。
     * @see xmax_logger_configure
     */
    static void configure(
        uint32_t options,
        xmax_log_language language = XMAX_LOG_CHINESE
    ) {
        check(xmax_logger_configure(options, language));
    }

    /**
     * 读取进程级日志选项和输出语言。
     *
     * @return 日志选项位掩码与输出语言组成的二元组。
     * @see xmax_logger_get_config
     */
    static std::pair<uint32_t, xmax_log_language> configuration() {
        uint32_t options = 0;
        xmax_log_language language = XMAX_LOG_CHINESE;
        check(xmax_logger_get_config(&options, &language));

        return {options, language};
    }

    /**
     * 非阻塞消费最早的日志记录。
     *
     * @param[out] record 接收已脱敏的日志记录；本方法自动设置 struct_size。
     * @return 取得记录返回 true；当前无记录返回 false。
     * @see xmax_logger_poll
     */
    static bool poll(
        xmax_log_record& record
    ) {
        record.struct_size = sizeof(record);
        const auto result = xmax_logger_poll(&record);
        check(result);
        return result == XMAX_OK;
    }
};
} // namespace xmax
