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
 * The uploaded object's URL, object key, and optional ETag.
 */
struct StoredFile {

    /**
     * The URL of the successfully uploaded file.
     */
    std::string url;

    /**
     * The storage object key used to locate the uploaded object.
     */
    std::string object_key;

    /**
     * The uploaded object's ETag, or an empty string if absent from the response.
     */
    std::string etag;
};
} // namespace xmax::detail
