// EventManager.hpp

#ifndef EVENT_MANAGER_HPP
#define EVENT_MANAGER_HPP

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <type_traits>
#include <algorithm>
#include <AtomicStack.hpp>

namespace utils
{
template <typename T, typename = void>
struct hasCountType : std::false_type {};
template <typename T>
struct hasCountType<T, std::void_t<decltype(T::COUNT)>> : std::true_type {};

template <typename CbType, typename... Args>
class EventManager
{
    static_assert(std::is_enum_v<CbType>, "CbType must be an enum type.");
    static_assert(std::is_same<typename std::underlying_type<CbType>::type, uint8_t>::value, "CbType must have 'uint8_t' as its underlying type.");
    static_assert(hasCountType<CbType>::value, "CbType must define a 'COUNT' member at the end.");
public:
    using Callback = void(*)(void*, Args&...);

    struct Id
    {
        Id(uint32_t d) : id(d) {}
        operator uint32_t() { return id; }
    private:
        friend class EventManager;
        uint32_t id;
    };

    struct Ctx
    {
        void* ctx;
        Callback fn;
        uint32_t id;
    };

    Id registerCallback(CbType type, void* ctx, Callback fn)
    {
        std::lock_guard<std::mutex> lock(mtx);
        uint32_t id{};
        if (!unusedIds.pop(id))
            id = nextId++;
        callbacksByType[static_cast<uint8_t>(type)].push_back({ctx, fn, id});
        idToType[id] = type;
        return Id{id};
    }

    void unregister(Id id)
    {
        std::lock_guard<std::mutex> lock(mtx);
        unregisterImpl(id.id);
    }

    void unregister(std::vector<uint32_t>& ids)
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& id : ids)
            unregisterImpl(id);
    }

    void run(CbType type, Args&... args)
    {
        std::vector<Ctx> snap;
        {
            std::lock_guard<std::mutex> lock(mtx);
            snap = callbacksByType[static_cast<uint8_t>(type)];
        }
        for (auto& c : snap)
            c.fn(c.ctx, args...);
    }

private:

    void unregisterImpl(uint32_t id)
    {
        auto itType = idToType.find(id);
        if (itType == idToType.end()) return;

        CbType type = itType->second;
        auto& vec = callbacksByType[static_cast<uint8_t>(type)];
        vec.erase(std::remove_if(vec.begin(), vec.end(), [id](const Ctx& c){ return c.id == id; }), vec.end());
        idToType.erase(itType);

        unusedIds.push(id);
    }

    std::mutex mtx;
    std::vector<Ctx> callbacksByType[static_cast<uint8_t>(CbType::COUNT)];
    std::unordered_map<uint32_t, CbType> idToType;
    uint32_t nextId = 1;
    types::AtomicStack<uint32_t> unusedIds;
};
}

#endif // EVENT_MANAGER_HPP
