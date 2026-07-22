// EigrpTypes.cpp

#include <EnumBitMap.hpp>

#include "EigrpTypes.hpp"
#include "configs/registry/router/EigrpRegistry.h"
#include "configs/FieldAccessor.hpp"

namespace routing::eigrp
{
KValue getKValues(const config::EigrpRegistry& configs)
{
    return KValue(
        configs.get<config::Eigrp::WEIGHT_K1>().load(),
        configs.get<config::Eigrp::WEIGHT_K2>().load(),
        configs.get<config::Eigrp::WEIGHT_K3>().load(),
        configs.get<config::Eigrp::WEIGHT_K4>().load(),
        configs.get<config::Eigrp::WEIGHT_K5>().load()
    );
}

StubConfig getStubConfig(const config::EigrpRegistry& configs)
{
    StubConfig s;
    auto stubField = configs.get<config::Eigrp::STUB>();
    s.isStub = stubField.hasValue();
    if (s.isStub) {
        types::EnumBitMap<config::eigrp::Stub> bm(stubField.load());
        s.advertiseConnected     = bm.test(config::eigrp::Stub::CONNECTED);
        s.advertiseStatic        = bm.test(config::eigrp::Stub::STATIC);
        s.advertiseSummary       = bm.test(config::eigrp::Stub::SUMMARY);
        s.advertiseRedistributed = bm.test(config::eigrp::Stub::REDISTRIBUTED);
        s.receiveOnly            = bm.test(config::eigrp::Stub::RECEIVE_ONLY);
    }
    auto leakMap = configs.get<config::Eigrp::STUB_LEAK_MAP>();
    s.advertiseLeakMap = leakMap.hasValue();
    return s;
}
} // namespace routing::eigrp
