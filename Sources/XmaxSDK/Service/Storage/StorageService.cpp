//
//  StorageService.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Service/Storage/StorageService.hpp"
#include "Foundation/Crypto/Sha1.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace xmax::detail {
namespace {

/**
 * Executes storage byte requests and redacts transport errors while credential protection is active.
 *
 * @param[in] transport The shared HTTP byte transport.
 * @param[in] request The HTTP request with response limits; callbacks remain valid until return.
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 *
 * @return The completed byte response; transport failures are redacted and rethrown.
 */
HttpResponse storage_request(
    Http& transport,
    const HttpRequest& request,
    const Cancel& cancel
) {
    const auto started = Clock::now();
    try {
        auto response = transport.request(
            request,
            cancel
        );

        XmaxLogger::write(
            LogCategory::Storage,
            response.status >= 200 && response.status < 300 ? XMAX_LOG_DEBUG : XMAX_LOG_ERROR,
            [&](auto language) {
                return XmaxLogger::localized(
                    language,
                    "存储请求 (Storage Request)",
                    "Storage Request"
                ) +
                       "\n├─ " + request.method + ", HTTP " + std::to_string(response.status) + "\n└─ " +
                       XmaxLogger::localized(
                           language,
                           "耗时：",
                           "Duration: "
                       ) +
                       std::to_string(
                           std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started)
                               .count()
                       ) +
                       " ms";
            }
        );
        return response;
    } catch (const Error& error) {
        // Retain credential guards while redacting exceptions so upload secrets remain registered.
        const auto safe = XmaxLogger::redact(error.what());

        XmaxLogger::write(
            LogCategory::Storage,
            XMAX_LOG_ERROR,
            [&](auto language) {
                return XmaxLogger::localized(
                    language,
                    "存储请求失败 (Storage Request Failed)",
                    "Storage Request Failed"
                ) +
                       "\n└─ " + safe;
            }
        );

        throw Error(
            error.code,
            safe
        );
    }
}

/**
 * The maximum image upload size, 20 MiB.
 */
constexpr uint64_t image_limit = 20 * 1024 * 1024;

/**
 * The maximum other-media upload and download size, 256 MiB.
 */
constexpr uint64_t transfer_limit = 256 * 1024 * 1024;

/**
 * Lowercases ASCII metadata for case-insensitive MIME and host matching.
 *
 * @param[in] value The ASCII metadata to lowercase.
 */
std::string lower(
    std::string value
) {
    for (auto& c : value)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

/**
 * Percent-encodes signature or object-key bytes, optionally preserving path slashes.
 *
 * @param[in] value The raw text bytes used for signing or object identification.
 * @param[in] slash Whether to preserve path-separator slashes without percent encoding.
 */
std::string encoded(
    const std::string& value,
    bool slash = false
) {
    const char* hex = "0123456789ABCDEF";
    std::string result;

    for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~' || (slash && c == '/'))
            result += char(c);
        else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }

    return result;
}

/**
 * Validates HTTPS hosts and rejects user information and unsafe URL characters.
 *
 * @param[in] url The absolute HTTPS URL without user information.
 */
std::string authority(
    const std::string& url
) {
    require(
        url.rfind(
            "https://",
            0
        ) == 0 &&
            url.find_first_of("\r\n\\#") == std::string::npos,
        XMAX_INVALID_ARGUMENT,
        "A valid HTTPS URL is required"
    );

    const auto end = url.find_first_of(
        "/?",
        8
    );
    const auto host = url.substr(
        8,
        end == std::string::npos ? end : end - 8
    );
    require(
        !host.empty() && host.find('@') == std::string::npos,
        XMAX_INVALID_ARGUMENT,
        "Invalid HTTPS authority"
    );

    return lower(host);
}

/**
 * Reads a required storage credential field and rejects HTTP-header injection characters.
 *
 * @param[in] value The JSON object containing temporary storage credentials.
 * @param[in] field The required credential field name.
 */
std::string required(
    const json& value,
    const char* field
) {
    require(
        value.contains(field) && value[field].is_string() &&
            !value[field].get_ref<const std::string&>().empty(),
        XMAX_API_ERROR,
        "Storage credential response is incomplete"
    );

    const auto text = value[field].get<std::string>();
    require(
        text.find_first_of("\r\n") == std::string::npos,
        XMAX_API_ERROR,
        "Invalid storage credential value"
    );

    return text;
}

/**
 * Normalizes an object filename to safe ASCII and trims surrounding punctuation.
 *
 * @param[in] value The uploaded object filename to normalize.
 *
 * @return A nonempty safe filename for storage metadata.
 */
std::string safe_name(
    const std::string& value
) {
    std::string result;

    for (unsigned char c : value) {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                           c == '.' || c == '-' || c == '_';
        if (valid)
            result += char(c);
        else if (!result.empty() && result.back() != '_')
            result += '_';
    }

    const auto first = result.find_first_not_of("._-");

    if (first == std::string::npos)
        return {};

    return result.substr(
        first,
        result.find_last_not_of("._-") - first + 1
    );
}
} // namespace

/**
 * Infers a supported image or video MIME type from the filename extension.
 *
 * @param[in] name The filename used for object metadata and MIME type inference.
 *
 * @return A supported MIME type; unsupported extensions throw an SDK error.
 */
std::string StorageService::content_type(
    const std::string& name
) {
    static const std::map<std::string, std::string> types{
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"jpe", "image/jpeg"},
        {"png", "image/png"},
        {"gif", "image/gif"},
        {"webp", "image/webp"},
        {"heic", "image/heic"},
        {"heif", "image/heif"},
        {"bmp", "image/bmp"},
        {"svg", "image/svg+xml"},
        {"tif", "image/tiff"},
        {"tiff", "image/tiff"},
        {"avif", "image/avif"},
        {"mp4", "video/mp4"},
        {"mov", "video/quicktime"},
        {"m4v", "video/x-m4v"},
        {"webm", "video/webm"},
        {"avi", "video/x-msvideo"},
        {"mkv", "video/x-matroska"},
        {"3gp", "video/3gpp"},
        {"3g2", "video/3gpp2"},
        {"ts", "video/mp2t"}
    };
    const auto dot = name.find_last_of('.');
    const auto found = types.find(dot == std::string::npos ? "" : lower(name.substr(dot + 1)));
    require(
        found != types.end(),
        XMAX_INVALID_ARGUMENT,
        "Cannot infer the media content type"
    );

    return found->second;
}

/**
 * Uploads encoded bytes with temporary storage credentials and an optional image safety check.
 *
 * @param[in] data The encoded bytes to process; they must remain valid until this synchronous call returns.
 * @param[in] name The filename used for object metadata and MIME type inference.
 * @param[in] type An optional MIME type; empty infers the type from the filename.
 * @param[in] safety Whether to perform a safety check when uploading an image.
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 * @param[in] progress The byte-progress callback, receiving completed bytes followed by total bytes.
 *
 * @return An owned upload result containing the URL, object key, and optional ETag.
 */
StoredFile StorageService::upload(
    const std::string& data,
    std::string name,
    std::string type,
    bool safety,
    const Cancel& cancel,
    const TransferProgress& progress
) {
    require(
        !cancel(),
        XMAX_CANCELLED,
        "Upload cancelled"
    );

    if (type.empty())
        type = content_type(name);

    type = lower(type);

    const bool image = type.rfind(
        "image/",
        0
    ) == 0,
               video = type.rfind(
                   "video/",
                   0
               ) == 0;
    require(
        (image || video) && type.find_first_of("\r\n") == std::string::npos,
        XMAX_INVALID_ARGUMENT,
        "Expected an image or video content type"
    );
    require(
        !safety || image,
        XMAX_INVALID_ARGUMENT,
        "Image safety checks require an image"
    );
    name = safe_name(name);
    require(
        !name.empty() && name.size() <= 512 && !data.empty() &&
            data.size() <= (image ? image_limit : transfer_limit),
        XMAX_INVALID_ARGUMENT,
        "Invalid media filename or size (images: 20 MiB, videos: 256 MiB)"
    );

    const auto sts = api_->request(
        "GET",
        "/cos/sts",
        nullptr,
        cancel
    );
    const auto bucket = required(
        sts,
        "bucket"
    ),
               region = required(
                   sts,
                   "region"
               );
    require(
        bucket.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos &&
            region.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos,
        XMAX_API_ERROR,
        "Invalid COS bucket or region"
    );
    require(
        sts.contains("credentials") && sts["credentials"].is_object(),
        XMAX_API_ERROR,
        "Missing storage credentials"
    );

    const auto& credentials = sts["credentials"];
    const auto key = required(
        credentials,
        "accessKeyId"
    ),
               secret = required(
                   credentials,
                   "secretAccessKey"
               ),
               token = required(
                   credentials,
                   "sessionToken"
               );
    const auto log_key = XmaxLogger::protect_secret(key);
    const auto log_secret = XmaxLogger::protect_secret(secret);
    const auto log_token = XmaxLogger::protect_secret(token);
    XmaxLogger::write(
        LogCategory::Storage,
        XMAX_LOG_INFO,
        [&](auto language) {
            return XmaxLogger::localized(
                language,
                "开始上传 (Upload Started)",
                "Upload Started"
            ) +
                   "\n└─ bytes=" + std::to_string(data.size());
        }
    );
    require(
        sts.contains("prefix") && sts["prefix"].is_string(),
        XMAX_API_ERROR,
        "Missing storage prefix"
    );

    auto prefix = sts["prefix"].get<std::string>();
    require(
        prefix.find("..") == std::string::npos && prefix.find_first_of("\r\n\\?#") == std::string::npos &&
            (prefix.empty() || prefix.front() != '/'),
        XMAX_API_ERROR,
        "Invalid storage prefix"
    );

    if (!prefix.empty() && prefix.back() != '/')
        prefix += '/';

    auto endpoint = sts.value(
        "endpoint",
        std::string{}
    );

    if (endpoint.empty())
        endpoint = "https://" + bucket + ".cos." + region + ".myqcloud.com";

    if (endpoint.find("://") == std::string::npos)
        endpoint = "https://" + endpoint;

    auto host = authority(endpoint);
    require(
        endpoint.find_first_of(
            "/?",
            8
        ) == std::string::npos ||
            endpoint.substr(endpoint.find_first_of(
                "/?",
                8
            )) == "/",
        XMAX_API_ERROR,
        "COS endpoint must not include a path or query"
    );

    if (host.rfind(
        bucket + ".",
        0
    ) != 0)
        host = bucket + "." + host;

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    const auto object = prefix +
                        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count()) +
                        "_" + random_id() + "_" + name;
    const auto path = "/" + encoded(
        object,
        true
    );
    const auto time = std::to_string(seconds - 60) + ";" + std::to_string(seconds + 900);
    const auto canonical = "put\n" + path + "\n\nhost=" + encoded(host) + "\n";
    const auto sign =
        hmac_sha1_hex(
            hmac_sha1_hex(
                secret,
                time
            ),
            "sha1\n" + time + "\n" + sha1_hex(canonical) + "\n"
        );
    HttpRequest request;
    request.method = "PUT";
    request.url = "https://" + host + path;
    request.body = data;
    request.timeout_ms = 60000;
    request.progress = progress;
    request.headers = {
        {"Content-Type", type},
        {"x-cos-security-token", token},
        {"Authorization",
         "q-sign-algorithm=sha1&q-ak=" + encoded(key) + "&q-sign-time=" + time + "&q-key-time=" + time +
             "&q-header-list=host&q-url-param-list=&q-signature=" + sign}
    };

    const auto response = storage_request(
        *api_->transport(),
        request,
        cancel
    );
    require(
        !cancel(),
        XMAX_CANCELLED,
        "Upload cancelled"
    );
    if (response.status < 200 || response.status >= 300)
        throw Error(
            XMAX_UPLOAD_ERROR,
            "COS upload was rejected",
            0,
            int32_t(response.status)
        );

    StoredFile result{request.url, object, {}};

    for (const auto& header : response.headers)
        if (lower(header.first) == "etag")
            result.etag = header.second;

    if (safety) {
        const json body{{"url", result.url}};
        const auto checked = api_->request(
            "POST",
            "/cos/image/check",
            &body,
            cancel
        );
        require(
            checked.contains("safe") && checked["safe"].is_boolean(),
            XMAX_API_ERROR,
            "Invalid image safety result"
        );
        require(
            checked["safe"].get<bool>(),
            XMAX_UNSAFE_IMAGE,
            "The image did not pass the safety check"
        );
        result.url = required(
            checked,
            "url"
        );
        authority(result.url);
    }

    progress(data.size(), data.size());
    XmaxLogger::write(
        LogCategory::Storage,
        XMAX_LOG_INFO,
        [&](auto language) {
            return XmaxLogger::localized(
                language,
                "上传完成 (Upload Completed)",
                "Upload Completed"
            ) +
                   "\n└─ bytes=" + std::to_string(data.size());
        }
    );

    return result;
}

/**
 * Reads a local encoded image or video and uploads it using the supplied metadata.
 *
 * @param[in] path The UTF-8 path to an encoded local image or video file.
 * @param[in] name The filename used for object metadata and MIME type inference.
 * @param[in] type An optional MIME type; empty infers the type from the filename.
 * @param[in] safety Whether to perform a safety check when uploading an image.
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 * @param[in] progress The byte-progress callback, receiving completed bytes followed by total bytes.
 *
 * @return An owned upload result containing the URL, object key, and optional ETag.
 */
StoredFile StorageService::upload_file(
    const std::string& path,
    std::string name,
    std::string type,
    bool safety,
    const Cancel& cancel,
    const TransferProgress& progress
) {
    const auto file = std::filesystem::u8path(path);

    if (name.empty())
        name = file.filename().u8string();

    if (type.empty())
        type = content_type(name);

    std::error_code error;
    const auto size = std::filesystem::file_size(
        file,
        error
    );
    require(
        !error && size > 0 && size <= (type.rfind(
            "image/",
            0
        ) == 0 ? image_limit : transfer_limit),
        XMAX_IO_ERROR,
        "Cannot read media file or file exceeds the upload limit"
    );

    std::ifstream stream(
        file,
        std::ios::binary
    );
    require(
        bool(stream),
        XMAX_IO_ERROR,
        "Cannot open media file"
    );

    std::string data(
        size_t(size),
        '\0'
    );

    for (size_t offset = 0; offset < data.size();) {
        require(
            !cancel(),
            XMAX_CANCELLED,
            "Upload cancelled"
        );

        const auto count = std::min(size_t(65536), data.size() - offset);
        stream.read(
            data.data() + offset,
            std::streamsize(count)
        );
        require(
            size_t(stream.gcount()) == count,
            XMAX_IO_ERROR,
            "Media file changed while reading"
        );
        offset += count;
    }

    return upload(
        data,
        std::move(name),
        std::move(type),
        safety,
        cancel,
        progress
    );
}

/**
 * Downloads HTTPS content to a temporary file and publishes it to a nonexistent destination on success.
 *
 * @param[in] url An absolute HTTPS URL allowed by the transport's URL policy.
 * @param[in] path The UTF-8 destination path; the file must not already exist.
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 * @param[in] progress The byte-progress callback, receiving completed bytes followed by total bytes.
 *
 * @return The size of the completed destination file, in bytes.
 */
uint64_t StorageService::download(
    const std::string& url,
    const std::string& path,
    const Cancel& cancel,
    const TransferProgress& progress
) {
    authority(url);
    require(
        !path.empty(),
        XMAX_INVALID_ARGUMENT,
        "A download destination is required"
    );

    const auto destination = std::filesystem::u8path(path);
    require(
        !std::filesystem::exists(destination),
        XMAX_IO_ERROR,
        "Download destination already exists"
    );

    const auto temporary = std::filesystem::u8path(path + ".part-" + random_id());
    uint64_t bytes = 0;

    try {
        std::ofstream stream(
            temporary,
            std::ios::binary | std::ios::trunc
        );
        require(
            bool(stream),
            XMAX_IO_ERROR,
            "Cannot create download temporary file"
        );

        HttpRequest request;
        request.method = "GET";
        request.url = url;
        request.timeout_ms = 120000;
        request.response_limit = transfer_limit;
        request.progress = progress;
        request.receive = [&](const char* data, size_t count) {
            require(
                bytes + count <= transfer_limit,
                XMAX_IO_ERROR,
                "Download exceeds 256 MiB"
            );
            stream.write(
                data,
                std::streamsize(count)
            );
            require(
                bool(stream),
                XMAX_IO_ERROR,
                "Download file write failed"
            );
            bytes += count;
        };

        const auto response = storage_request(
            *api_->transport(),
            request,
            cancel
        );
        require(
            !cancel(),
            XMAX_CANCELLED,
            "Download cancelled"
        );
        if (response.status < 200 || response.status >= 300)
            throw Error(
                XMAX_DOWNLOAD_ERROR,
                "Download was rejected",
                0,
                int32_t(response.status)
            );
        stream.flush();
        require(
            bool(stream),
            XMAX_IO_ERROR,
            "Download file flush failed"
        );
        stream.close();
#ifdef _WIN32
        require(
            MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH),
            XMAX_IO_ERROR,
            "Cannot publish download; destination may exist"
        );
#else
        require(
            ::link(
                temporary.c_str(),
                destination.c_str()
            ) == 0,
            XMAX_IO_ERROR,
            "Cannot publish download; destination may exist"
        );
        std::filesystem::remove(temporary);
#endif
        progress(bytes, bytes);
        XmaxLogger::write(
            LogCategory::Storage,
            XMAX_LOG_INFO,
            [&](auto language) {
                return XmaxLogger::localized(
                    language,
                    "下载完成 (Download Completed)",
                    "Download Completed"
                ) +
                       "\n└─ bytes=" + std::to_string(bytes);
            }
        );

        return bytes;
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(
            temporary,
            ignored
        );
        throw;
    }
}
} // namespace xmax::detail
