//
//  uploaded_file_info.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include <string>

namespace xmax {

/**
 * Upload-result metadata copied into app-owned storage.
 */
struct uploaded_file_info {

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
} // namespace xmax
