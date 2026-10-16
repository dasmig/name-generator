// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_RESOURCES_HPP
#define DASMIG_CORE_RESOURCES_HPP

#include "dataset.hpp"
#include "tsv.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

/// @file resources.hpp
/// @brief Finding a library's data: compiled-in files, or a `resources/`
///        directory found on disk.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)

namespace dasmig::core
{

/// @brief The directory name of a data tier.
/// @param tier The tier.
/// @return `"lite"` or `"full"`.
[[nodiscard]] constexpr std::string_view tier_name(dataset tier) noexcept
{
    return tier == dataset::full ? "full" : "lite";
}

/// @brief A data file compiled into the program.
///
/// Generated headers (`scripts/embed.py`) define arrays of these, so a build
/// without a filesystem (WebAssembly) still has its data. The content is
/// split into chunks because compilers limit the size of one string literal
/// (MSVC: about 64 KB).
struct embedded_file
{
    std::string_view path;                     ///< Path below `resources/`, e.g. `lite/biodata.tsv`.
    std::span<const std::string_view> chunks;  ///< The content, in order.

    /// @brief The whole content.
    /// @return The chunks joined.
    [[nodiscard]] std::string text() const
    {
        std::size_t size = 0;
        for (const auto chunk : chunks) size += chunk.size();
        std::string out;
        out.reserve(size);
        for (const auto chunk : chunks) out += chunk;
        return out;
    }
};

/// @brief Find a compiled-in file by path.
/// @param files The embedded files (empty when none are compiled in).
/// @param path Path below `resources/`, with `/` separators.
/// @return The file, or nullptr.
[[nodiscard]] constexpr const embedded_file* find_embedded(std::span<const embedded_file> files,
                                                           std::string_view path) noexcept
{
    for (const auto& file : files)
    {
        if (file.path == path) return &file;
    }
    return nullptr;
}

/// @brief The directories searched for a library's `resources/`, in order.
///
/// Relative to @p base: `resources`, `../resources`, `<library>/resources`
/// and `../<library>/resources`. These cover running from the repository,
/// from its build directory, and from a parent directory holding several
/// libraries side by side.
///
/// @param library Repository name, e.g. `biodata-generator`.
/// @param base Directory to search from (default: the working directory).
/// @return The candidate directories.
[[nodiscard]] inline std::vector<std::filesystem::path> resource_candidates(
    std::string_view library, const std::filesystem::path& base = std::filesystem::current_path())
{
    const std::filesystem::path lib{library};
    return {base / "resources", base / ".." / "resources", base / lib / "resources",
            base / ".." / lib / "resources"};
}

/// @brief The first candidate directory that contains a marker file.
///
/// The marker tells a library's `resources/` apart from another library's
/// (for example `lite/biodata.tsv`).
///
/// @param library Repository name, e.g. `biodata-generator`.
/// @param marker A file path below `resources/` that the library ships.
/// @param base Directory to search from (default: the working directory).
/// @return The `resources/` directory, or empty when none is found.
// A swapped library and marker fails on the first lookup (a marker is a path
// such as lite/x.tsv), so the adjacent string parameters are kept.
[[nodiscard]] inline std::optional<std::filesystem::path> find_resources(
    std::string_view library, std::string_view marker, // NOLINT(bugprone-easily-swappable-parameters)
    const std::filesystem::path& base = std::filesystem::current_path())
{
    for (const auto& dir : resource_candidates(library, base))
    {
        std::error_code ec;
        if (std::filesystem::is_regular_file(dir / marker, ec)) return dir;
    }
    return std::nullopt;
}

/// @brief Load a data file, compiled-in if available, otherwise from disk.
///
/// Embedded files win, so a build with compiled-in data never depends on the
/// working directory. The two give the same tsv_document::content_hash(),
/// so seeds replay the same either way.
///
/// @param path Path below `resources/`, e.g. `lite/biodata.tsv`.
/// @param embedded Compiled-in files (empty when none).
/// @param root A `resources/` directory (from find_resources()), or empty.
/// @param columns Column names for a headerless file; empty when the first
///        line names them.
/// @return The document, or empty when the file is neither embedded nor
///         present under @p root.
/// @throws tsv_error If the file exists but cannot be parsed.
[[nodiscard]] inline std::optional<tsv_document> load_resource(
    std::string_view path, std::span<const embedded_file> embedded,
    const std::optional<std::filesystem::path>& root, std::vector<std::string> columns = {})
{
    if (const auto* file = find_embedded(embedded, path))
    {
        std::string source = "embedded:" + std::string{path};
        if (columns.empty()) return tsv_document::parse(file->text(), std::move(source));
        return tsv_document::parse(file->text(), std::move(source), std::move(columns));
    }
    if (!root) return std::nullopt;
    const auto file = *root / path;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return std::nullopt;
    return tsv_document::load(file, std::move(columns));
}

} // namespace dasmig::core

#endif // DASMIG_CORE_RESOURCES_HPP
