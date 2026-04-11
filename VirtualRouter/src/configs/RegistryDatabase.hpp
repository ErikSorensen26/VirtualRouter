/**
 * @file RegistryDatabase.hpp
 * @brief Minimal runtime registry that allocates and owns config objects.
 */

#ifndef REGISTRY_DATABASE_HPP
#define REGISTRY_DATABASE_HPP

#include <list>
#include <memory>
#include <mutex>

#include "RegistryTypes.hpp"

namespace config
{

/**
 * @brief Root registry that allocates and owns protocol config objects.
 *
 * Provides stable references via std::list (no iterator/pointer invalidation
 * on insert). create<T>() allocates a new T; emplaceBack delegates to the
 * OwnedListField; emplace() is a no-op registration helper that just returns
 * the passed reference.
 *
 * @tparam Entries  Unused parameter pack (kept for compatibility with the
 *                  `using Registry = RegistryDatabase<...>` alias).
 */
template <typename... Entries>
class RegistryDatabase
{
    struct ErasedBase { virtual ~ErasedBase() = default; };
    template<typename T>
    struct Holder : ErasedBase { T value; };

    std::list<std::unique_ptr<ErasedBase>> storage;
    std::mutex mu;

public:
    /**
     * @brief Allocate a new T, store it, and return a stable reference.
     *
     * Any additional arguments are silently ignored (e.g. an instance ID that
     * the old reference-counted implementation used as a key).
     */
    template<typename T, typename... Args>
    T& create(Args&&...)
    {
        std::lock_guard lk(mu);
        auto h = std::make_unique<Holder<T>>();
        T& ref = h->value;
        storage.push_back(std::move(h));
        return ref;
    }

    /**
     * @brief Append a new child entry to an OwnedListField and return it.
     *
     * Delegates directly to OwnedListField::emplaceBack so that the list's
     * own applier callbacks and key-map logic are exercised correctly.
     */
    template<IsOwnedListField Field>
    typename Field::type& emplaceBack(Field& field, const typename Field::key& k)
    {
        return field.emplaceBack(k);
    }

    /**
     * @brief Register an existing config object (single-argument overload).
     *
     * In the old reference-counted implementation this registered an object
     * with a parent scope.  Here it is a no-op that simply returns the passed
     * reference so that reference-member initializers keep compiling.
     */
    template<typename T>
    T& emplace(T& existing)
    {
        return existing;
    }

    /**
     * @brief Register an existing config object with a parent (two-argument overload).
     */
    template<typename T, typename P>
    T& emplace(T& existing, P&)
    {
        return existing;
    }
};

} // namespace config

#endif // REGISTRY_DATABASE_HPP
