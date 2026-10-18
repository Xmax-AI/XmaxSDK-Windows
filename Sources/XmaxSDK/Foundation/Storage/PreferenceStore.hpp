#pragma once
#include "Foundation/Common.hpp"
#include <filesystem>
#include <optional>
namespace xmax::detail {
class PreferenceStore {
  public:
    virtual ~PreferenceStore() = default;
    virtual std::optional<std::string> read(const std::string& key) noexcept = 0;
    virtual void write(const std::string& key, const std::string& value) noexcept = 0;
};
// Empty directory selects the current user's application cache. No credentials are stored.
std::shared_ptr<PreferenceStore> make_preference_store(std::filesystem::path directory = {});
} // namespace xmax::detail
