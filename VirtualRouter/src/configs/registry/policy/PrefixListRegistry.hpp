/**
 * @file PrefixListRegistry.hpp
 * @brief Prefix-list configuration schema (permit/deny ordered prefix entries).
 * @ingroup CONFIG_POLICY
 */

#ifndef PREFIX_LIST_REGISTRY_HPP
#define PREFIX_LIST_REGISTRY_HPP

#include <IPAddress.h>

#include "configs/RegistryTypes.hpp"
#include "configs/SubRegistry.hpp"

namespace config
{
/**
 * @brief Fields for a named prefix-list entry set.
 * @ingroup CONFIG_POLICY
 */
enum class PrefixList
{
    DESCRIPTION, ///< Human-readable description of this prefix-list.
    PERMIT,      ///< Ordered permit entries: (seq, prefix, ge, le).
    DENY,        ///< Ordered deny entries: (seq, prefix, ge, le).
    COUNT
};

template <types::IsIPPrefix P>
struct PrefixListFields : FieldTuple<
    ValueField<std::string>,
    ListField<std::tuple<uint32_t, IGNOR(P), IGNOR(uint8_t), IGNOR(uint8_t)>>,
    ListField<std::tuple<uint32_t, IGNOR(P), IGNOR(uint8_t), IGNOR(uint8_t)>>
> {};

/**
 * @brief Registry slot for one named prefix-list (IPv4 or IPv6).
 * @ingroup CONFIG_POLICY
 *
 * Each entry tuple is `(sequence, prefix, ge, le)` where `ge`/`le` constrain
 * the matched prefix length range relative to the base prefix.
 *
 * @tparam P  Prefix type — either `types::IPv4Prefix` or `types::IPv6Prefix`.
 */
template <types::IsIPPrefix P>
struct PrefixListRegistry : SubRegistry<PrefixListRegistry<P>, PrefixList, nullptr, PrefixListFields<P>> {};
}

#endif // PREFIX_LIST_REGISTRY_HPP
