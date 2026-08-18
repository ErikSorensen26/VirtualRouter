/**
 * @file Any.hpp
 * @brief Minimal type-erased, non-owning pointer wrapper (a raw-pointer `std::any`).
 * @ingroup UTILS
 *
 * Stores a `void*` alongside its `std::type_index` (keyed on the pointer type,
 * e.g. `typeid(T*)`) so it can later be recovered with @ref Any::cast. Holds no
 * ownership over the pointee; the caller is responsible for its lifetime.
 */

#ifndef ANY_HPP
#define ANY_HPP

#include <typeindex>
#include <type_traits>

namespace utils
{
class Any
{
public:
    Any() noexcept = default;

    template <typename T>
    Any(T* raw) noexcept
    {
        set(raw);
    }

    template <typename T>
    void set(T* raw) noexcept
    {
        ptr = const_cast<std::remove_cv_t<T>*>(raw);
        idx = typeid(T*);
    }

    bool hasValue() const noexcept
    {
        return ptr != nullptr;
    }

    void reset() noexcept
    {
        ptr = nullptr;
        idx = typeid(void);
    }

    std::type_index type() const noexcept
    {
        return idx; 
    }

    template <typename T>
    static T cast(const Any& op)
    {
        static_assert(std::is_pointer_v<T>, "Template parameter must be a pointer type (e.g., int*)");
        if (op.type() == typeid(T))
            return static_cast<T>(op.ptr);
        return nullptr;
    }
private:

    void* ptr;
    std::type_index idx = typeid(void);
};
}

#endif // ANY_HPP
