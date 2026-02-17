// RegistryDefaultTable.hpp

#ifndef REGISTRY_DEFAULT_TABLE_HPP
#define REGISTRY_DEFAULT_TABLE_HPP

#include <type_traits>

namespace Config
{
template <typename ENUM, ENUM E>
struct Entry
{
    static constexpr bool has = false;

    template <typename T>
    static constexpr T get() noexcept
    {
        if constexpr (std::is_pointer_v<T>)
            return nullptr;
        else
            return T{};
    }
};

template <typename ENUM, ENUM E>
inline constexpr bool hasV = Entry<ENUM, E>::has;

template <typename ENUM, ENUM E, typename T>
constexpr T getV() noexcept
{
    static_assert(Entry<ENUM, E>::has, "No default exists for this enum entry.");
    return Entry<ENUM, E>::template get<T>();
}

#define CONFIG_DEFAULT_ROW(EnumType, EnumValue, Literal) \
    template <> \
    struct Entry<EnumType, EnumType::EnumValue> \
    { \
        static constexpr bool has = true; \
        template <typename T> \
        static constexpr T get() noexcept \
        { \
            return static_cast<T>(Literal); \
        } \
    };

#define CONFIG_DEFAULT_TABLE(TABLE_MACRO) \
    TABLE_MACRO(CONFIG_DEFAULT_ROW)
}

#endif // REGISTRY_DEFAULT_TABLE_HPP
