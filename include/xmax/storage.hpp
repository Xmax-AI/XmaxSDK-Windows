//
//  storage.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "handle_owner.hpp"
#include "error.hpp"
#include "uploaded_file.hpp"
#include <string>

namespace xmax {

/**
 * 提供可取消的同步上传和下载；每个实例同一时间只执行一次传输。
 */
class storage : public detail::owner<xmax_storage, xmax_storage_destroy> {

  public:

    /**
     * 继承句柄所有者的构造接口，接管传入句柄；空句柄表示空对象。
     *
     * @note 不复制底层资源，销毁时调用与句柄匹配的释放接口。
     */
    using owner::owner;

    /**
     * 请求取消活动传输，允许从其他线程调用。
     *
     * @see xmax_storage_cancel
     */
    void cancel() const noexcept {
        xmax_storage_cancel(get());
    }

    /**
     * 读取线程安全的传输进度快照。
     *
     * @return 传输状态与字节计数快照；总字节数未知时为 0。
     * @see xmax_storage_get_progress
     */
    xmax_transfer_progress progress() const {
        xmax_transfer_progress value{};
        value.struct_size = sizeof(value);
        check(xmax_storage_get_progress(get(), &value));
        return value;
    }

    /**
     * 在工作线程上传本地编码图片或视频文件。
     *
     * @param[in] path UTF-8 本地编码图片或视频路径。
     * @param[in] check_safety 是否对图片上传执行安全检查。
     * @param[in] content_type 可选 MIME 类型，空文本按文件名推断。
     * @return 独立拥有的上传结果，包含文件访问地址、对象键和可选 ETag。
     * @see xmax_storage_upload_file
     */
    uploaded_file upload_file(
        const std::string& path,
        bool check_safety = false,
        const std::string& content_type = {}
    ) {
        const xmax_upload_options options{
            sizeof(options), nullptr, content_type.empty() ? nullptr : content_type.c_str(), check_safety
        };
        xmax_uploaded_file result = nullptr;
        check(xmax_storage_upload_file(get(), path.c_str(), &options, &result));

        return uploaded_file(result);
    }

    /**
     * 上传编码文件字节；该接口不接收原始视频像素。
     *
     * @param[in] data 待处理的编码字节，本次同步调用返回前必须有效。
     * @param[in] size 可访问或待处理的字节数。
     * @param[in] name 文件名称，用于对象元数据及 MIME 类型推断。
     * @param[in] type 可选 MIME 类型，空文本按文件名推断。
     * @param[in] check_safety 是否对图片上传执行安全检查。
     * @return 独立拥有的上传结果，包含文件访问地址、对象键和可选 ETag。
     * @see xmax_storage_upload_data
     */
    uploaded_file upload_data(
        const uint8_t* data,
        size_t size,
        const std::string& name,
        const std::string& type = {},
        bool check_safety = false
    ) {
        const xmax_upload_options options{
            sizeof(options), name.c_str(), type.empty() ? nullptr : type.c_str(), check_safety
        };
        xmax_uploaded_file result = nullptr;
        check(xmax_storage_upload_data(get(), data, size, &options, &result));

        return uploaded_file(result);
    }

    /**
     * 下载 HTTPS 内容到新文件，不覆盖已有目标文件。
     *
     * @param[in] url 完整 HTTPS 地址，需满足当前传输接口的地址策略。
     * @param[in] destination UTF-8 本地目标文件路径，必须尚不存在。
     * @return 成功下载的文件字节数。
     * @see xmax_storage_download
     */
    uint64_t download(
        const std::string& url,
        const std::string& destination
    ) {
        uint64_t bytes = 0;
        check(xmax_storage_download(get(), url.c_str(), destination.c_str(), &bytes));
        return bytes;
    }
};
} // namespace xmax
