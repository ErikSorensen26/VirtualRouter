/**
 * @file Evaluator.hpp
 * @brief Interpreter sketch for JIT-compiled packet policies.
 * @ingroup POLICY_JIT
 */

#ifndef POLICY_EVALUATOR_HPP
#define POLICY_EVALUATOR_HPP

#include <cstdint>
#include <vector>

/**
 * @namespace policy::compiler
 * @brief Simpler, alternate instruction set for packet policies (see PolicyJit.h for the JIT form).
 */
namespace policy::compiler
{
/** @brief Comparison operators for a match predicate. */
enum class Op : uint8_t
{
    EQ,
    NEQ,
    LT,
    GT,
    BIT,
    MASK
};

/** @brief One match predicate: compare @c size bytes at @c offset against @c value. */
struct alignas(16) Instruction
{
    uint8_t opcode;
    uint8_t size;
    uint16_t offset;
    uint64_t value;
};

/** @brief A program is an ordered list of instructions. */
using InstructionSet = std::vector<Instruction>;

/**
 * @brief Interprets a program against a packet.
 *
 * @warning Stub — the loop body is empty and no result is returned; not
 * functional yet.
 */
[[nodiscard]] inline bool evaluate(const std::vector<Instruction>& prog, const uint8_t* pkt)
{
    for (const Instruction& i : prog)
    {
    }
}
}

#endif // POLICY_EVALUATOR_HPP
