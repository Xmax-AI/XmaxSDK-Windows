#pragma once
#include "Service/Network/ApiService.hpp"
namespace xmax::detail {
struct StoredFile {
    std::string url, object_key, etag;
};
using TransferProgress = std::function<void(uint64_t, uint64_t)>;
class StorageService {
  public:
    explicit StorageService(std::shared_ptr<ApiService> api) : api_(std::move(api)) {}
    StoredFile upload(const std::string& data, std::string name, std::string content_type, bool safety,
                      const Cancel&, const TransferProgress&);
    StoredFile upload_file(const std::string& path, std::string name, std::string type, bool safety,
                           const Cancel&, const TransferProgress&);
    uint64_t download(const std::string& url, const std::string& path, const Cancel&,
                      const TransferProgress&);
    static std::string content_type(const std::string& name);

  private:
    std::shared_ptr<ApiService> api_;
};
} // namespace xmax::detail
