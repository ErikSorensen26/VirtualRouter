// FilterEngine.hpp

#ifndef FILTER_ENGINE_HPP
#define FILTER_ENGINE_HPP

#include "DebuggerOptions.hpp"

class FilterEngine
{
public:
    FilterEngine(const DebuggerOptions& opts) : options(opts) {}

    bool allowFile(const std::string& file) const
    {
        for (const auto& regex : options.fileIncludes)
            if (std::regex_match(file, regex)) return true;
        return options.fileIncludes.empty();
    }

    bool allowFunction(const std::string& name) const
    {
        for (const auto& regex : options.functionIncludes)
            if (std::regex_match(name, regex)) return true;
        return options.functionIncludes.empty();
    }

private:
    const DebuggerOptions& options;
};

#endif // FILTER_ENGINE_HPP
