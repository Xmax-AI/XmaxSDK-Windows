//
//  uploaded_file.hpp
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
#include "uploaded_file_info.hpp"

namespace xmax {

/**
 * Exclusively owns an upload-result handle and provides metadata copies.
 */
class uploaded_file : public detail::owner<xmax_uploaded_file, xmax_uploaded_file_release> {

  public:

    /**
     * Inherits the handle owner's constructors; an empty handle creates an empty owner.
     *
     * @note The underlying resource is not copied. Destruction invokes its matching release function.
     */
    using owner::owner;

    /**
     * Copies upload metadata into app-owned strings.
     *
     * @return Copies of the URL, object key, and ETag; missing ETags become empty strings.
     *
     * @see xmax_uploaded_file_get_info
     */
    uploaded_file_info info() const {
        xmax_uploaded_file_info result{};
        result.struct_size = sizeof(result);
        check(xmax_uploaded_file_get_info(
            get(),
            &result
        ));

        return {result.url, result.object_key, result.etag ? result.etag : ""};
    }
};
} // namespace xmax
