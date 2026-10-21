//
//  StorageService.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Storage/StoredFile.hpp"
#include "Service/Network/ApiService.hpp"

namespace xmax::detail {

/**
 * 字节进度回调，参数依次为已完成字节数与总字节数，未知总数为 0。
 */
using TransferProgress = std::function<void(uint64_t, uint64_t)>;

/**
 * 执行临时凭证获取、文件上传、可选图片安全检查及有大小限制的下载。
 */
class StorageService {

  public:

    /**
     * 保留执行临时凭证、安全校验和文件传输所需的共享 API 服务。
     */
    explicit StorageService(
        std::shared_ptr<ApiService> api
    )
        : api_(std::move(api)) {
    }

    /**
     * 使用临时存储凭证上传编码字节，可选择执行图片安全检查。
     *
     * @param[in] data 待处理的编码字节，本次同步调用返回前必须有效。
     * @param[in] name 文件名称，用于对象元数据及 MIME 类型推断。
     * @param[in] content_type 可选 MIME 类型，空文本按文件名推断。
     * @param[in] safety 是否对图片上传执行安全检查。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @param[in] progress 字节进度回调，参数顺序为已完成字节数、总字节数。
     * @return 独立拥有的上传结果，包含文件访问地址、对象键和可选 ETag。
     */
    StoredFile upload(
        const std::string& data,
        std::string name,
        std::string content_type,
        bool safety,
        const Cancel& cancel,
        const TransferProgress& progress
    );

    /**
     * 读取本地编码图片或视频，并根据元数据配置执行上传。
     *
     * @param[in] path UTF-8 本地编码图片或视频路径。
     * @param[in] name 文件名称，用于对象元数据及 MIME 类型推断。
     * @param[in] type 可选 MIME 类型，空文本按文件名推断。
     * @param[in] safety 是否对图片上传执行安全检查。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @param[in] progress 字节进度回调，参数顺序为已完成字节数、总字节数。
     * @return 独立拥有的上传结果，包含文件访问地址、对象键和可选 ETag。
     */
    StoredFile upload_file(
        const std::string& path,
        std::string name,
        std::string type,
        bool safety,
        const Cancel& cancel,
        const TransferProgress& progress
    );

    /**
     * 将 HTTPS 内容写入临时文件，成功后发布到不存在的目标路径。
     *
     * @param[in] url 完整 HTTPS 地址，需满足当前传输接口的地址策略。
     * @param[in] path UTF-8 本地目标文件路径，目标必须尚不存在。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @param[in] progress 字节进度回调，参数顺序为已完成字节数、总字节数。
     * @return 成功发布的目标文件字节数。
     */
    uint64_t download(
        const std::string& url,
        const std::string& path,
        const Cancel& cancel,
        const TransferProgress& progress
    );

    /**
     * 根据文件扩展名推断支持的图片或视频 MIME 类型。
     *
     * @param[in] name 文件名称，用于对象元数据及 MIME 类型推断。
     * @return 受支持的 MIME 类型；扩展名不支持时抛出 SDK 错误。
     */
    static std::string content_type(
        const std::string& name
    );

  private:

    /**
     * 保留共享 API 服务，使子控制器在客户端句柄释放后仍可使用。
     */
    std::shared_ptr<ApiService> api_;
};
} // namespace xmax::detail
