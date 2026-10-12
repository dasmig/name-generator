#ifndef DASMIG_CORE_HPP
#define DASMIG_CORE_HPP

/// @file core.hpp
/// @brief dasmig-core: the shared foundation of the dasmig generator
///        libraries (random engine, distributions, data tables, JSON).
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.
///
/// Libraries vendor this header and the `core/` directory next to their own
/// header, include it, and check compatibility with DASMIG_CORE_REQUIRE.

#include "core/version.hpp"

#include "core/dataset.hpp"
#include "core/distributions.hpp"
#include "core/json.hpp"
#include "core/math.hpp"
#include "core/resources.hpp"
#include "core/rng.hpp"
#include "core/seed_signature.hpp"
#include "core/table.hpp"
#include "core/tsv.hpp"
#include "core/utf8.hpp"
#include "core/weighted_table.hpp"

#endif // DASMIG_CORE_HPP
