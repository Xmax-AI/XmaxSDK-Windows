//
//  ClientHandle.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Core/XmaxClient.hpp"

/** Owns a C client's configuration and services. Private transport injection supports offline ABI tests. */
struct xmax_client_impl {
    xmax::detail::XmaxClient client;

    explicit xmax_client_impl(
        xmax::detail::Config config,
        std::shared_ptr<xmax::detail::Http> http = xmax::detail::make_http()
    )
        : client(std::move(config), std::move(http)) {
    }
};
