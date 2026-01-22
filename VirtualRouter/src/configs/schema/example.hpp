

#include <RegistryTypes.hpp>
#include <SubRegistry.hpp>
#include <vector>

namespace Config
{
enum class InterfaceConfigs : uint8_t
{
    IP_ADDRESS,
    MTU,
    Neighbors,
    COUNT,
};


SubRegistry<InterfaceConfigs,
    AtomicField<uint32_t, 0xFFFFFFFF, InterfaceConfigs::IP_ADDRESS>,
    AtomicField<uint16_t, 1500, InterfaceConfigs::MTU>,
    VariableField<std::vector<int>, InterfaceConfigs::Neighbors>
> configMgr;

MaskSubRegistry<decltype(configMgr)> sdfsdf(configMgr);

void buh()
{
    configMgr.template get<InterfaceConfigs::IP_ADDRESS>().set(4);
    sdfsdf.template get<InterfaceConfigs::IP_ADDRESS>().unset();
    auto neighbors = configMgr.template get<InterfaceConfigs::Neighbors>().get();
    auto neighbors2 = sdfsdf.template get<InterfaceConfigs::Neighbors>().get();
}
}
