// EnumRegistry.hpp
#ifndef ENUM_REGISTRY_HPP
#define ENUM_REGISTRY_HPP

#include <string_view>
#include <cstdint>

// --- Internal helper macros ---
#define ENUMREG_ENUM_DECL(name, str)   name,
#define ENUMREG_STRING_DECL(name, str) str,
#define ENUMREG_CASE_DECL(name, str)   case types::hash(str): return Enum::name;

namespace types
{

constexpr uint64_t hash(std::string_view str)
{
    uint64_t h = 1469598103934665603ULL;
    for (char c : str)
    {
        h ^= static_cast<uint64_t>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

} // namespace types

/**
 * Main Macro
 *
 * Usage:
 * #define COMMAND_LIST(X) \
 *     X(SHOW, "show") \
 *     X(CONFIGURE, "configure")
 *
 * DECLARE_ENUM_REGISTRY(Command, COMMAND_LIST)
 */
#define DECLARE_ENUM_REGISTRY(EnumName, LIST_MACRO)                     \
                                                                        \
    enum class EnumName                                                 \
    {                                                                   \
        LIST_MACRO(ENUMREG_ENUM_DECL)                                   \
        COUNT,                                                          \
        UNKNOWN                                                         \
    };                                                                  \
                                                                        \
    struct EnumName##Util                                               \
    {                                                                   \
        using Enum = EnumName;                                          \
                                                                        \
        static constexpr std::array<std::string_view,                   \
            static_cast<std::size_t>(Enum::COUNT)> strings =            \
        {                                                               \
            LIST_MACRO(ENUMREG_STRING_DECL)                             \
        };                                                              \
                                                                        \
        static constexpr std::string_view toString(Enum value)          \
        {                                                               \
            std::size_t idx = static_cast<std::size_t>(value);          \
            return idx < strings.size() ? strings[idx] : "unknown";     \
        }                                                               \
                                                                        \
        static constexpr Enum fromString(std::string_view str)          \
        {                                                               \
            switch (types::hash(str))                                   \
            {                                                           \
                LIST_MACRO(ENUMREG_CASE_DECL)                           \
                default: return Enum::UNKNOWN;                          \
            }                                                           \
        }                                                               \
                                                                        \
        static constexpr bool isValid(Enum value)                       \
        {                                                               \
            return static_cast<std::size_t>(value)                      \
                 < static_cast<std::size_t>(Enum::COUNT);               \
        }                                                               \
    };

#endif // ENUM_REGISTRY_HPP
