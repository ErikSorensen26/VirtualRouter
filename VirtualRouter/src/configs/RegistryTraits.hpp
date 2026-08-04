/**
 * @file RegistryTraits.hpp
 * @brief The maps from a config enum to the things generated alongside it.
 * @ingroup CONFIG
 *
 * A config enum is the name of a registry. Three separate things are generated
 * from that same name, and each needs its own way back:
 *
 *   RegistryOf<ENUM>    -> the SubRegistry type holding the fields
 *   HashRegistry<ENUM>  -> the array of field-name hashes
 *   Entry<ENUM, FIELD>  -> the declared default, in RegistryDefaults.hpp
 *
 * DEFINE_CONFIG_GROUP emits all three from one FIELD_LIST, so they stay in step
 * by construction. They are declared here rather than in the builder because
 * SubRegistry.hpp needs them and the builder includes SubRegistry.hpp; putting
 * them in the builder would close that cycle.
 *
 * Each primary template is left undefined. A registry that has not been
 * converted to the X-macro form therefore fails to compile at the point of use
 * rather than resolving to an empty table that silently answers nothing -- with
 * hasRegistryV and hasHashesV available for code that must tolerate the gap.
 */

#ifndef REGISTRY_TRAITS_HPP
#define REGISTRY_TRAITS_HPP

#include <type_traits>

#include "configs/ConfigMeta.hpp" // IWYU pragma: export -- Strip, DefType, tokenHash

namespace config
{
/// @brief Maps a config enum to its generated table of field-name hashes.
template <typename ENUM>
struct HashRegistry;

/**
 * @brief Maps a config enum to the registry type that holds its fields.
 *
 * The two have always corresponded -- DEFINE_CONFIG_GROUP emits Arp beside
 * ArpRegistry -- but only as a naming convention that a macro could recover by
 * token pasting. Anything generating over a list of enums has no ## to reach
 * for, so the correspondence is written down here instead.
 *
 * Not every enum has one. PrefixListRegistry<P> is a template, one instantiation
 * per prefix family, so PrefixList names no single type and is deliberately left
 * unspecialized; check hasRegistryV before naming it.
 */
template <typename ENUM>
struct RegistryOf;

/// @brief The registry type for a config enum; ill-formed unless hasRegistryV.
template <typename ENUM>
using RegistryOfT = typename RegistryOf<ENUM>::type;

/// @brief True when a config enum maps to exactly one registry type.
template <typename ENUM, typename = void>
inline constexpr bool hasRegistryV = false;

template <typename ENUM>
inline constexpr bool hasRegistryV<ENUM, std::void_t<typename RegistryOf<ENUM>::type>> = true;
}

#endif // REGISTRY_TRAITS_HPP
