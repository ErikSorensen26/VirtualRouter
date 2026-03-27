/**
 * @file AttributeManager.hpp
 * @brief BGP path attributes: AS path, next-hop, MED, local preference.
 */

/**
 * @defgroup BGP_ATTRIBUTES BGP Attributes
 * @ingroup BGP
 * @brief Path attribute storage, reference counting, and attribute type definitions.
 */

#ifndef BGP_ATTRIBUTE_MANAGER_HPP
#define BGP_ATTRIBUTE_MANAGER_HPP

#include <unordered_map>
#include <cstdint>
#include <deque>
#include <vector>
#include <cassert>

#include "AttributeTypes.hpp"

namespace routing::bgp
{
/**
 * @brief BGP path attributes: AS path, next-hop, MED, local preference, communities.
 * @ingroup BGP_ATTRIBUTES
 *
 * Manages BGP UPDATE message attributes including flags, type code, length,
 * and value. Handles all well-known and optional attributes per RFC 4271.
 */
class AttributeManager
{
public:
    uint32_t acquire(const Attributes& attrs, const Path& path)
    {
        uint32_t attrId = 0;
        auto ait = attrToId.find(attrs);
        if (ait != attrToId.end())
        {
            attrId = ait->second;
        }
        else
        {
            if (!freeAttrIds.empty())
            {
                attrId = freeAttrIds.back();
                freeAttrIds.pop_back();
            }
            else
            {
                attrId = static_cast<uint32_t>(idToAttr.size());
                idToAttr.emplace_back();
            }

            AttrEntry& attrEntry = idToAttr[attrId];
            attrEntry.attrs = attrs;
            attrEntry.refCount = 0;
            attrEntry.used = true;

            attrToId.emplace(attrEntry.attrs, attrId);
        }

        PathKey key{attrId, path};
        auto pit = pathToId.find(key);
        if (pit != pathToId.end())
        {
            const uint32_t pathId = pit->second;
            ++idToPath[pathId].refCount;
            return pathId;
        }

        AttrEntry& attrEntry = idToAttr[attrId];
        ++attrEntry.refCount;

        uint32_t pathId = 0;
        if (!freePathIds.empty())
        {
            pathId = freePathIds.back();
            freePathIds.pop_back();
        }
        else
        {
            pathId = static_cast<uint32_t>(idToPath.size());
            idToPath.emplace_back();
        }

        PathEntry& pathEntry = idToPath[pathId];
        pathEntry.path = path;
        pathEntry.attrId = attrId;
        pathEntry.refCount = 1;
        pathEntry.used = true;

        pathToId.emplace(key, pathId);

        return pathId;
    }

    bool retain(uint32_t id)
    {
        if (id >= idToPath.size())
            return false;

        PathEntry& entry = idToPath[id];
        if (!entry.used)
            return false;

        ++entry.refCount;
        return true;
    }

    void release(uint32_t id)
    {
        if (id >= idToPath.size())
            return;

        PathEntry& entry = idToPath[id];

        if (!entry.used)
            return;

        if (entry.refCount == 0)
            return;

        if (--entry.refCount > 0)
            return;

        PathKey key{entry.attrId, entry.path};

        pathToId.erase(key);

        releaseAttr(entry.attrId);

        entry.used = false;
        freePathIds.push_back(id);
    }

    const Attributes& getAttributes(uint32_t id) const
    {
        assert(id < idToPath.size());
        const PathEntry& pathEntry = idToPath[id];
        assert(pathEntry.used);
        assert(pathEntry.attrId < idToAttr.size());
        return idToAttr[pathEntry.attrId].attrs;
    }

    const Path& getPath(uint32_t id) const 
    {
        assert(id < idToPath.size());
        const PathEntry& pathEntry = idToPath[id];
        assert(pathEntry.used);
        return pathEntry.path;
    }

    PathAttribute get(uint32_t id) const
    {
        assert(id < idToPath.size());
        const PathEntry& pathEntry = idToPath[id];
        assert(pathEntry.used);
        assert(pathEntry.attrId < idToAttr.size());
        return PathAttribute{ idToAttr[pathEntry.attrId].attrs, pathEntry.path };
    }

    void clear()
    {
        attrToId.clear();
        idToAttr.clear();
        freeAttrIds.clear();

        pathToId.clear();
        idToPath.clear();
        freePathIds.clear();
    }

    void reserve(size_t n)
    {
        idToAttr.reserve(n);
        idToPath.reserve(n);
    }

private:

    void releaseAttr(uint32_t id)
    {
        if (id >= idToAttr.size())
            return;

        AttrEntry& entry = idToAttr[id];

        if (!entry.used)
            return;

        if (entry.refCount == 0)
            return;

        if (--entry.refCount > 0)
            return;

        attrToId.erase(entry.attrs);

        entry.used = false;
        freeAttrIds.push_back(id);
    }
private:

    struct AttrEntry
    {
        Attributes attrs{};
        uint32_t refCount = 0;
        bool used = false;
    };

    struct PathEntry
    {
        Path path{};
        uint32_t attrId = 0;
        uint32_t refCount = 0;
        bool used = false;
    };

    struct PathKey
    {
        uint32_t attrId;
        Path path;

        bool operator==(const PathKey& o) const noexcept = default;
    };

    struct PathKeyHash
    {
        size_t operator()(const PathKey& k) const noexcept
        {
            size_t h = std::hash<uint32_t>{}(k.attrId);
            h ^= std::hash<Path>{}(k.path) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<Attributes, uint32_t> attrToId;
    std::vector<AttrEntry> idToAttr;
    std::deque<uint32_t> freeAttrIds;

    std::unordered_map<PathKey, uint32_t, PathKeyHash> pathToId;
    std::vector<PathEntry> idToPath;
    std::deque<uint32_t> freePathIds;
};
} // namespace routing

#endif // BGP_ATTRIBUTE_MANAGER_HPP

