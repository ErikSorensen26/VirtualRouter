// TreePatch.cpp

#include "TreePatch.h"

#include <charconv>
#include <stdexcept>

namespace cli::tree
{
std::optional<uint64_t> portPlaceholderBase(std::string_view pattern)
{
    if (pattern.size() <= 2 || pattern.front() != '<' || pattern.back() != '>')
        return std::nullopt;

    std::string_view body = pattern.substr(1, pattern.size() - 2);
    uint64_t first = 0;
    auto [ptr, ec] = std::from_chars(body.data(), body.data() + body.size(), first);

    // A lone number is a placeholder; "<1-99>" is already a range and any
    // leftover characters mean this is some other pattern entirely.
    if (ec != std::errc{} || ptr != body.data() + body.size())
        return std::nullopt;

    return first;
}

std::string expandPortPlaceholder(std::string_view pattern, size_t count)
{
    if (count == 0) return {};

    std::optional<uint64_t> base = portPlaceholderBase(pattern);
    if (!base) return {};

    return "<" + std::to_string(*base) + "-"
         + std::to_string(*base + count - 1) + ">";
}


void TreePatch::patchName(uint32_t index, const CommandNode& base, std::string_view name)
{
    if (strs.size() >= ID_BIAS)
        throw std::runtime_error("cli::tree::TreePatch: patched string count exceeds the id space");

    const uint16_t id = static_cast<uint16_t>(ID_BIAS + strs.size());
    strs.push_back({static_cast<uint32_t>(blob.size()),
                    static_cast<uint32_t>(name.size())});
    blob.append(name);

    CommandNode patched = base;
    patched.nameId = id;
    overrides[index] = patched;
}

const CommandNode* TreePatch::find(uint32_t index) const
{
    auto it = overrides.find(index);
    return it == overrides.end() ? nullptr : &it->second;
}

std::string_view TreePatch::text(uint16_t id) const
{
    if (id < ID_BIAS) return {};

    const size_t slot = static_cast<size_t>(id) - ID_BIAS;
    if (slot >= strs.size()) return {};

    const StrRef& r = strs[slot];
    return std::string_view(blob.data() + r.off, r.len);
}
}
