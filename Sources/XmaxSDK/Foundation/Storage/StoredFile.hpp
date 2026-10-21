//
//  StoredFile.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"

namespace xmax::detail {

/**
 * 保存上传成功后的访问地址、对象键及可选 ETag。
 */
struct StoredFile {

    /**
     * 上传成功后的文件访问地址。
     */
    std::string url;

    /**
     * 存储服务返回的对象键，用于定位已上传对象。
     */
    std::string object_key;

    /**
     * 上传对象的 ETag；服务未返回该值时为空文本。
     */
    std::string etag;
};
} // namespace xmax::detail
