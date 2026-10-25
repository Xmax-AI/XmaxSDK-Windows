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
 * Provides process-wide logging configuration and nonblocking consumption independently of client or
 * realtime instances.
 */
class logger {

  public:

    /**
     * Sets process-wide logging options and language; logging is disabled by default.
     *
     * @param[in] options The business and performance logging bitmask; 0 disables logging.
     * @param[in] language The logging language: XMAX_LOG_CHINESE or XMAX_LOG_ENGLISH.
     *
     * @see xmax_logger_configure
     */
    static void configure(
        uint32_t options,
        xmax_log_language language = XMAX_LOG_CHINESE
    ) {
        check(xmax_logger_configure(
            options,
            language
        ));
    }

    /**
     * Returns process-wide logging options and output language.
     *
     * @return The logging bitmask and output-language pair.
     *
     * @see xmax_logger_get_config
     */
    static std::pair<uint32_t, xmax_log_language> configuration() {
        uint32_t options = 0;
        xmax_log_language language = XMAX_LOG_CHINESE;
        check(xmax_logger_get_config(
            &options,
            &language
        ));

        return {options, language};
    }

    /**
     * Consumes the oldest log record without blocking.
     *
     * @param[out] record Receives a redacted record; this method initializes struct_size.
     *
     * @return True if a record was retrieved; false if none is pending.
     *
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
