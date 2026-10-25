//
//  PreferenceStore.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include <filesystem>
#include <optional>

namespace xmax::detail {

/**
 * The noncredential persistence interface; read and write failures must not throw to callers.
 */
class PreferenceStore {

  public:

    /**
     * Releases the preference backend after all calls finish.
     *
     * @note Finish concurrent calls using this object before destroying it.
     */
    virtual ~PreferenceStore() = default;

    /**
     * Returns cached bytes, or std::nullopt for missing or unreadable entries.
     *
     * @param[in] key A valid noncredential preference key without path-traversal components.
     */
    virtual std::optional<std::string> read(
        const std::string& key
    ) noexcept = 0;

    /**
     * Persists noncredential preferences on a best-effort basis without throwing on failure.
     *
     * @param[in] key A valid noncredential preference key without path-traversal components.
     * @param[in] value The noncredential bytes to persist.
     */
    virtual void write(
        const std::string& key,
        const std::string& value
    ) noexcept = 0;
};

/**
 * Creates a noncredential cache backend; an empty directory selects the current user's application cache.
 *
 * @param[in] directory The noncredential cache root; empty selects the current user's application cache.
 *
 * @return A shared noncredential preference backend.
 */
std::shared_ptr<PreferenceStore> make_preference_store(
    std::filesystem::path directory = {}
);
} // namespace xmax::detail
