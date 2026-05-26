// Instructions.hpp

#ifndef POLICY_EVALUATOR_HPP
#define POLICY_EVALUATOR_HPP

#include <cstdint>
#include <vector>

namespace policy::compiler
{
enum class Op : uint8_t
{
    EQ,
    NEQ,
    LT,
    GT,
    BIT,
    MASK
};  

struct alignas(16) Instruction
{
    uint8_t opcode;
    uint8_t size;
    uint16_t offset;
    uint64_t value; 
};

using InstructionSet = std::vector<Instruction>;

[[nodiscard]] inline bool evaluate(const std::vector<Instruction>& prog, const uint8_t* pkt)
{
    for (const Instruction& i : prog)
    {
        
    }
}
}

#endif // POLICY_EVALUATOR_HPP
