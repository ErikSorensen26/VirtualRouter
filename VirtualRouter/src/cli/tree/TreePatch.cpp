// TreePatch.cpp

#include "TreePatch.h"

#include <charconv>

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


void TreePatch::patchName(uint32_t index, const CommandNode& base,
                          std::string_view name, std::string_view desc)
{
    // nameSiz is a byte. No CLI token comes close, but truncating keeps a bad
    // caller from silently wrapping the length to something shorter.
    if (name.size() > 0xFF) name = name.substr(0, 0xFF);

    // desc() reads at infoOff + nameSiz, so a node's name and description have
    // to stay adjacent. Moving the name alone would leave the description
    // resolving against the patch buffer at an offset that holds nothing, so
    // the description is copied in behind it.
    const size_t start = blob.size();
    blob.append(name);
    blob.append(desc);

    CommandNode patched = base;
    patched.infoOff = OFFSET_BIAS + static_cast<uint32_t>(start);
    patched.nameSiz = static_cast<uint8_t>(name.size());
    overrides[index] = patched;
}

const CommandNode* TreePatch::find(uint32_t index) const
{
    auto it = overrides.find(index);
    return it == overrides.end() ? nullptr : &it->second;
}
}
