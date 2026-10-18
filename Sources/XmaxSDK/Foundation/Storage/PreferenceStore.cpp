#include "Foundation/Storage/PreferenceStore.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include <cstdlib>
#include <fstream>
#include <mutex>
#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif
namespace xmax::detail {
namespace {
std::filesystem::path cache_directory() {
#ifdef _WIN32
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)))
        return {};
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(raw, CoTaskMemFree);
    return std::filesystem::path(raw) / L"XmaxSDK" / L"Cache";
#else
    const auto* user_directory = std::getenv("HOME");
    return user_directory ? std::filesystem::path(user_directory) / "Library/Caches/ai.xmax.sdk"
                          : std::filesystem::path{};
#endif
}
class FileStore final : public PreferenceStore {
    std::filesystem::path directory_;
    std::mutex mutex_;
    std::filesystem::path file(const std::string& key) const {
        require(
            !directory_.empty() && !key.empty() && key.size() < 200 &&
                key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") ==
                    std::string::npos &&
                key.find("..") == std::string::npos,
            XMAX_INVALID_ARGUMENT, "Invalid preference key");
        return directory_ / std::filesystem::u8path(key + ".json");
    }

  public:
    explicit FileStore(std::filesystem::path directory) : directory_(std::move(directory)) {}
    std::optional<std::string> read(const std::string& key) noexcept override {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto path = file(key);
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error || size > 4096)
                return {};
            std::ifstream stream(path, std::ios::binary);
            std::string value(size_t(size), '\0');
            stream.read(value.data(), std::streamsize(value.size()));
            if (!stream)
                return {};
            return value;
        } catch (...) {
            return {};
        }
    }
    void write(const std::string& key, const std::string& value) noexcept override {
        std::filesystem::path temporary;
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto destination = file(key);
            require(value.size() <= 4096, XMAX_INVALID_ARGUMENT, "Preference is too large");
            std::filesystem::create_directories(directory_);
            temporary = directory_ / std::filesystem::u8path(".pending-" + random_id());
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream.write(value.data(), std::streamsize(value.size()));
            stream.flush();
            require(bool(stream), XMAX_IO_ERROR, "Cannot write preference");
            stream.close();
#ifdef _WIN32
            require(MoveFileExW(temporary.c_str(), destination.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
                    XMAX_IO_ERROR, "Cannot publish preference");
#else
            std::filesystem::rename(temporary, destination);
#endif
        } catch (...) {
            if (!temporary.empty()) {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
            }
        }
    }
};
} // namespace
std::shared_ptr<PreferenceStore> make_preference_store(std::filesystem::path directory) {
    if (directory.empty())
        directory = cache_directory();
    return std::make_shared<FileStore>(std::move(directory));
}
} // namespace xmax::detail
