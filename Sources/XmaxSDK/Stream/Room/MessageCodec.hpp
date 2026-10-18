#pragma once
#include "Foundation/Json.hpp"
namespace xmax::detail {
std::vector<std::string> encode_message(const std::string&, const std::string& event_id);
} // namespace xmax::detail
