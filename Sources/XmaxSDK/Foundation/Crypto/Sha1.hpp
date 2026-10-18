#pragma once
#include "Foundation/Common.hpp"
namespace xmax::detail {
std::string sha1_hex(const std::string&);
std::string hmac_sha1_hex(const std::string& key, const std::string& data);
} // namespace xmax::detail
