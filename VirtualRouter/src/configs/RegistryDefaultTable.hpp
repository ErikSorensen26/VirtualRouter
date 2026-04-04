/**
 * @file RegistryDefaultTable.hpp
 */

// RegistryDefaultTable.hpp

#ifndef REGISTRY_DEFAULT_TABLE_HPP
#define REGISTRY_DEFAULT_TABLE_HPP

#include <type_traits>

namespace config
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

template <typename T, auto E>
constexpr T getV() noexcept
{
    using ENUM = decltype(E);
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

template <typename T>
using DefType = typename Strip<T>::type;
}

#endif // REGISTRY_DEFAULT_TABLE_HPP
