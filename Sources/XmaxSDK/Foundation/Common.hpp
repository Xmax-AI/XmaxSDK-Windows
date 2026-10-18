#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <xmax/xmax.h>

namespace xmax::detail {
using Clock = std::chrono::steady_clock;
using Cancel = std::function<bool()>;
struct Error : std::runtime_error {
    xmax_result code;
    Error(xmax_result c, std::string message) : std::runtime_error(std::move(message)), code(c) {}
};
inline void require(bool value, xmax_result code, const std::string& message) {
    if (!value)
        throw Error(code, message);
}
} // namespace xmax::detail
