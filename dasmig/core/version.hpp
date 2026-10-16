// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_CORE_VERSION_HPP
#define DASMIG_CORE_VERSION_HPP

/// @file version.hpp
/// @brief dasmig-core version and the compatibility check used by libraries.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)

// The version must be a macro: DASMIG_CORE_REQUIRE checks it at the
// preprocessor and static_assert level across vendored copies.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)

/// @brief Major version. Changes when generated output or the API changes
///        incompatibly.
#define DASMIG_CORE_VERSION_MAJOR 1
/// @brief Minor version. Changes when features are added compatibly.
#define DASMIG_CORE_VERSION_MINOR 0
/// @brief Patch version. Changes for fixes that keep output identical.
#define DASMIG_CORE_VERSION_PATCH 1

/// @brief Fails compilation unless the core in use is compatible with the
///        version a library was built against.
///
/// Every library vendors its own copy of the core. When two libraries are
/// included in one translation unit, the include guards keep only the first
/// copy, so a library must check that the copy actually in use is
/// compatible: same major version, and at least the minor version it needs.
///
/// @code
/// #include "core.hpp"
/// DASMIG_CORE_REQUIRE(1, 2);
/// @endcode
#define DASMIG_CORE_REQUIRE(major, minor)                                      \
    static_assert(DASMIG_CORE_VERSION_MAJOR == (major) &&                      \
                      DASMIG_CORE_VERSION_MINOR >= (minor),                    \
                  "dasmig-core version mismatch: the libraries included in "   \
                  "this translation unit vendor incompatible core versions")
// NOLINTEND(cppcoreguidelines-macro-usage)

namespace dasmig::core
{

/// @brief A semantic version.
struct version_t
{
    int major; ///< Major version.
    int minor; ///< Minor version.
    int patch; ///< Patch version.
};

/// @brief The version of the core in use.
inline constexpr version_t version{.major = DASMIG_CORE_VERSION_MAJOR,
                                   .minor = DASMIG_CORE_VERSION_MINOR,
                                   .patch = DASMIG_CORE_VERSION_PATCH};

} // namespace dasmig::core

#endif // DASMIG_CORE_VERSION_HPP
