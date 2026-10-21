#include "Core/Storage/XmaxStorageManager.hpp"
#include "Foundation/Crypto/Sha1.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include "Stream/Quality/QualityHistory.hpp"
#include "TestSupport.hpp"
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
struct StorageHttp : Http {
    std::mutex mutex;
    std::vector<HttpRequest> calls;
    std::atomic<bool> safe{true}, fail{false}, block{false}, uploading{false};
    bool invalid_endpoint = false;
    HttpResponse request(const HttpRequest& input, const Cancel& cancel) override {
        {
            std::lock_guard<std::mutex> lock(mutex);
            calls.push_back(input);
        }
        const auto response = [](json data) -> HttpResponse {
            return {200, json{{"success", true}, {"data", std::move(data)}}.dump()};
        };
        if (input.url.find("/cos/sts") != std::string::npos)
            return response(
                {{"bucket", "demo-123"},
                 {"region", "ap-test"},
                 {"prefix", "uploads/"},
                 {"endpoint", invalid_endpoint ? "http://insecure.test" : "https://cos.ap-test.myqcloud.com"},
                 {"credentials",
                  {{"accessKeyId", "id"}, {"secretAccessKey", "secret"}, {"sessionToken", "token"}}}});
        if (input.url.find("/cos/image/check") != std::string::npos) {
            CHECK(json::parse(input.body).contains("url"));
            return response({{"safe", safe.load()}, {"url", "https://approved.test/image.png"}});
        }
        uploading = true;
        if (input.progress)
            input.progress(1, input.body.empty() ? 4 : input.body.size());
        while (block && !cancel())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(!cancel(), XMAX_CANCELLED, "Cancelled by test");
        if (input.method == "GET") {
            CHECK(bool(input.receive));
            input.receive("data", 4);
            if (input.progress)
                input.progress(4, 4);
            return {fail ? 503 : 200, ""};
        }
        CHECK(input.method == "PUT");
        CHECK(input.url.find("https://demo-123.cos.ap-test.myqcloud.com/uploads/") == 0);
        bool auth = false, token = false, leaked_key = false;
        for (const auto& h : input.headers) {
            if (h.first == "Authorization")
                auth = h.second.find("q-sign-algorithm=sha1&q-ak=id") == 0 &&
                       h.second.find("q-signature=") != std::string::npos;
            if (h.first == "x-cos-security-token")
                token = h.second == "token";
            if (h.first == "X-Api-Key")
                leaked_key = true;
        }
        CHECK(auth && token && !leaked_key);
        return {fail ? 500 : 200, "", {{"ETag", "test-etag"}}};
    }
};
static std::shared_ptr<ApiService> api(const std::shared_ptr<Http>& http) {
    Config config;
    config.key = "secret-xmax-key";
    return std::make_shared<ApiService>(config, http);
}
static void crypto_vectors() {
    CHECK(sha1_hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(hmac_sha1_hex(std::string(20, '\x0b'), "Hi There") == "b617318655057264e28bc0b6fb378c8ef146be00");
    CHECK(hmac_sha1_hex("Jefe", "what do ya want for nothing?") ==
          "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
}
static void upload_and_safety() {
    auto http = std::make_shared<StorageHttp>();
    XmaxStorageManager storage(api(http));
    const uint8_t bytes[]{0, 1, 0, 255};
    auto file = storage.upload_data(bytes, sizeof(bytes), "reference.png", "", true);
    CHECK(file.url == "https://approved.test/image.png" && file.etag == "test-etag");
    CHECK(file.object_key.find("uploads/") == 0 &&
          file.object_key.find("reference.png") != std::string::npos);
    CHECK(storage.progress().state == XMAX_TRANSFER_SUCCEEDED && storage.progress().transferred == 4);
    CHECK(http->calls[1].body.size() == 4 && http->calls[1].body[2] == 0);
    http->safe = false;
    expect_error(XMAX_UNSAFE_IMAGE, [&] { storage.upload_data(bytes, 4, "a.png", "", true); });
    CHECK(storage.progress().state == XMAX_TRANSFER_FAILED);
    const auto count = http->calls.size();
    expect_error(XMAX_INVALID_ARGUMENT, [&] { storage.upload_data(bytes, 4, "a.txt", "", false); });
    expect_error(XMAX_INVALID_ARGUMENT, [&] { storage.upload_data(bytes, 4, "a.mp4", "video/mp4", true); });
    CHECK(http->calls.size() == count);
    http->invalid_endpoint = true;
    expect_error(XMAX_INVALID_ARGUMENT, [&] { storage.upload_data(bytes, 4, "a.png", "", false); });
    CHECK(http->calls.size() == count + 1); // Only STS; no credentials sent to the invalid endpoint.
}
struct Folder {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / std::filesystem::u8path("xmax-tests-" + random_id());
    Folder() { std::filesystem::create_directory(path); }
    ~Folder() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
static void upload_file_and_atomic_download() {
    Folder folder;
    const auto source = folder.path / "input.png";
    {
        std::ofstream f(source, std::ios::binary);
        f << "image";
    }
    auto http = std::make_shared<StorageHttp>();
    XmaxStorageManager storage(api(http));
    CHECK(!storage.upload_file(source.u8string(), "", "", false).url.empty());
    const auto destination = folder.path / "result.png";
    CHECK(storage.download("https://files.test/result.png", destination.u8string()) == 4);
    std::ifstream stream(destination, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(stream), {}};
    CHECK(text == "data");
    stream.close();
    expect_error(XMAX_IO_ERROR, [&] { storage.download("https://files.test/a", destination.u8string()); });
    http->fail = true;
    const auto failed = folder.path / "failed.png";
    try {
        storage.download("https://files.test/a", failed.u8string());
        CHECK(false);
    } catch (const Error& error) {
        CHECK(error.code == XMAX_DOWNLOAD_ERROR && error.http_status == 503);
    }
    CHECK(!std::filesystem::exists(failed));
    for (const auto& entry : std::filesystem::directory_iterator(folder.path))
        CHECK(entry.path().filename().u8string().find(".part-") == std::string::npos);
}
static void quality_history() {
    Folder folder;
    auto store = make_preference_store(folder.path);
    QualityHistory current("x2.1", store), isolated("test-model", store);
    CHECK(current.read(false, 1000) == 2);
    current.write(4, false, 1000);
    current.write(3, true, 1001);
    CHECK(current.read(false, 1002) == 4 && current.read(true, 1002) == 3);
    CHECK(isolated.read(false, 1002) == 2);
    QualityHistory restarted("x2.1", make_preference_store(folder.path));
    CHECK(restarted.read(false, 1002) == 4);
    CHECK(restarted.read(false, 999) == 2 && restarted.read(false, 4600) == 2);
    store->write("quality-agora-x2.1-uplink", "{corrupt");
    CHECK(restarted.read(false, 1002) == 2);
    store->write("quality-agora-x2.1-uplink", R"({"level":99,"time":1000})");
    CHECK(restarted.read(false, 1002) == 2);
    store->write("../escape", "invalid");
    CHECK(!store->read("../escape"));
    for (const auto& entry : std::filesystem::directory_iterator(folder.path))
        CHECK(entry.path().filename().u8string().find(".pending-") != 0);
}
static void cancel_transfer() {
    auto http = std::make_shared<StorageHttp>();
    http->block = true;
    XmaxStorageManager storage(api(http));
    const uint8_t data[]{1, 2, 3, 4};
    auto operation = std::async(std::launch::async, [&] {
        expect_error(XMAX_CANCELLED, [&] { storage.upload_data(data, 4, "a.png", "", false); });
    });
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (!http->uploading && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(http->uploading);
    expect_error(XMAX_INVALID_STATE, [&] { storage.upload_data(data, 4, "b.png", "", false); });
    storage.cancel();
    operation.get();
    CHECK(storage.progress().state == XMAX_TRANSFER_CANCELLED);
    http->block = false;
    CHECK(!storage.upload_data(data, 4, "again.png", "", false).url.empty());
    CHECK(storage.progress().state == XMAX_TRANSFER_SUCCEEDED);
}
int main() {
    int failures = 0;
    for (auto test : std::vector<std::pair<const char*, void (*)()>>{
             {"SHA1 and HMAC standard vectors", crypto_vectors},
             {"upload and image safety", upload_and_safety},
             {"file upload and atomic download", upload_file_and_atomic_download},
             {"cancellation and busy admission", cancel_transfer},
             {"persistent quality history and expiry", quality_history}}) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << e.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
