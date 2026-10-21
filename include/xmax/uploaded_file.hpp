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
 * 以独占所有权管理上传结果句柄，可读取其元数据副本。
 */
class uploaded_file : public detail::owner<xmax_uploaded_file, xmax_uploaded_file_release> {

  public:

    /**
     * 继承句柄所有者的构造接口，接管传入句柄；空句柄表示空对象。
     *
     * @note 不复制底层资源，销毁时调用与句柄匹配的释放接口。
     */
    using owner::owner;

    /**
     * 将上传结果元数据复制到调用方拥有的字符串。
     *
     * @return 访问地址、对象键及 ETag 的副本，缺失 ETag 时为空字符串。
     * @see xmax_uploaded_file_get_info
     */
    uploaded_file_info info() const {
        xmax_uploaded_file_info result{};
        result.struct_size = sizeof(result);
        check(xmax_uploaded_file_get_info(get(), &result));

        return {result.url, result.object_key, result.etag ? result.etag : ""};
    }
};
} // namespace xmax
