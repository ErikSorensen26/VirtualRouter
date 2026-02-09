// RegistryTemplate.hpp

#ifndef REGISTRY_TEMPLATE_HPP
#define REGISTRY_TEMPLATE_HPP

#include "RegistryReference.hpp"

namespace Config
{
template <typename... Entries>
class RegistryDatabase;

template <typename KEY, typename ENUM, typename Ctx, typename... Fields>
class SubRegistry;

template <typename KEY, typename ENUM, typename... Fields>
class SimpleSubRegistry;

template <typename T, T D, auto F, typename Ctx = void, auto H = nullptr>
class AtomicField;

template <typename T, auto F, typename Ctx = void, auto H = nullptr>
class OptionalAtomicField;

template <typename T, auto F, typename Ctx = void, auto H = nullptr>
class ValueField;

template <typename T, auto F, typename Ctx = void, auto H = nullptr>
class OptionalValueField;

template <typename T, typename K, auto F>
class OwnedListField;
}

#endif // REGISTRY_TEMPLATE_HPP
