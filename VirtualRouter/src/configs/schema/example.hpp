

#include <RegistryTypes.hpp>

namespace Config
{
inline constexpr FieldDef<uint16_t> COST{{1, "cost"}, 10};
inline constexpr FieldDef<bool> PREFIX_SUPPRESSION{{2, "prefixsuppression"}, false};

inline constexpr FieldBase* INTERFACE_CONFIG[] = {
    FIELD(COST),
    FIELD(PREFIX_SUPPRESSION)
}

}
