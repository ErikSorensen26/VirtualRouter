/**
 * @file ConfigMeta.hpp
 * @brief Small type and string utilities the config layer builds on.
 * @ingroup CONFIG
 *
 * Nothing here knows what a registry is. It is separated out because these are
 * used from every layer -- field kinds, accessors, the tree generator -- and
 * threading them through a header about defaults or traits made those headers
 * look like they owned concepts they do not.
 */

#ifndef CONFIG_META_HPP
#define CONFIG_META_HPP

#include <cstdint>
#include <string_view>

namespace config
{
/**
 * @brief Peels pointers, references and const off a field type.
 *
 * A field is declared with the type the user thinks in -- `uint16_t`, `IPPrefix&`,
 * `const std::string` -- but the registry stores a plain value. This recovers
 * that value type, and recurses so combinations like `const T*` land on T.
 */
template <typename T>
struct Strip
{
    using type = T;
};

template <typename T>
struct Strip<T*>
{
    using type = typename Strip<T>::type;
};

template <typename T>
struct Strip<T&>
{
    using type = typename Strip<T>::type;
};

template <typename T>
struct Strip<T&&>
{
    using type = typename Strip<T>::type;
};

template <typename T>
struct Strip<const T>
{
    using type = typename Strip<T>::type;
};

/// @brief The value type a field actually stores.
template <typename T>
using DefType = typename Strip<T>::type;

/**
 * @brief FNV-1a over a name, used to look registries and fields up by string.
 *
 * The tree generator resolves "Ospf::areaType" to a pair of integers at build
 * time by hashing both halves and searching the generated tables, so this must
 * stay a constant expression and must not change: a different hash function
 * would silently renumber every binding in an existing command tree binary.
 */
constexpr uint32_t tokenHash(std::string_view sv)
{
    uint32_t h = 0x811C9DC5;
    for (size_t i = 0; i < sv.size(); ++i)
    {
        h ^= static_cast<uint32_t>(sv[i]);
        h *= 0x01000193;
    }
    return h;
}
}

#endif // CONFIG_META_HPP
