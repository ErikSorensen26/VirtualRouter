// OspfTypes.hpp

#ifndef OSPF_TYPES_HPP
#define OSPF_TYPES_HPP

#include <cstdint>

static constexpr uint16_t OSPF_MAX_AGE = 3600;
static constexpr uint16_t OSPF_REFRESH_AGE = 1800;
static constexpr uint16_t OSPF_MAX_DIFF = 900;

static constexpr uint16_t OSPF_HELLO_TIME = 10;
static constexpr uint16_t OSPF_MU_HELLO_TIME = 30;

static constexpr uint32_t OSPF_INITIAL_SEQUENCE = 0x80000001u;
static constexpr uint32_t OSPF_MAX_SEQUENCE     = 0x7FFFFFFFu;

#endif // OSPF_TYPES_HPP
