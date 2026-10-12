#ifndef DASMIG_CORE_DATASET_HPP
#define DASMIG_CORE_DATASET_HPP

#include <cstdint>

/// @file dataset.hpp
/// @brief The data tier enum shared by every generator library.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)

// Libraries released before the core define their own copy of the enum behind
// this guard; defining it here makes them skip it when included together.
#ifndef DASMIG_DATASET_DEFINED
#define DASMIG_DATASET_DEFINED

namespace dasmig
{

/// @brief Which tier of a library's bundled data to load.
enum class dataset : std::uint8_t
{
    lite, ///< Smaller tier: the best-covered entries, suitable for embedding.
    full  ///< Complete tier.
};

} // namespace dasmig

#endif // DASMIG_DATASET_DEFINED

#endif // DASMIG_CORE_DATASET_HPP
