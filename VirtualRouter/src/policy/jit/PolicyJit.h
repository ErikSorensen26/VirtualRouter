/**
 * @file PolicyJit.h
 * @brief JIT compiler for packet match/action policies.
 * @ingroup POLICY_JIT
 */

/**
 * @defgroup POLICY_JIT Policy JIT
 * @brief JIT-compiled packet policies: match predicates, actions, and executable output.
 *
 * Evaluation model:
 *   A Policy is an ordered list of Sequences.  Each Sequence has:
 *     - an ordered list of match predicates (all must pass — AND semantics)
 *     - an action: PERMIT or DENY
 *   Sequences are evaluated in declaration order (first-match).
 *   If no sequence matches, Policy::defaultAction is returned.
 *
 * Field sizes (all multi-byte fields read as big-endian / network order):
 *   S1 =  8-bit  native byte
 *   S2 = 16-bit  MOVBE/REV16
 *   S3 = 24-bit  load32 + bswap + shr8
 *   S4 = 32-bit  MOVBE/REV
 *   S6 = 48-bit  load64 + bswap + shr16
 *   S8 = 64-bit  MOVBE/REV64
 *
 * Compiles to x86-64 and ARM64.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <stdexcept>
#include <string>

namespace policy::jit {

// ── Field widths ──────────────────────────────────────────────────────────────

/** @brief Field width of a match operand in bytes. */
enum class Size : uint8_t {
    S1 = 1,
    S2 = 2,
    S3 = 3,
    S4 = 4,
    S6 = 6,
    S8 = 8,
};

// ── Predicate operators ───────────────────────────────────────────────────────

/** @brief Comparison operators for a single match predicate. */
enum class Operator : uint8_t { EQ, NEQ, LT, GT, MASK };

// ── Instruction ───────────────────────────────────────────────────────────────

/** @brief One match predicate: read @c size bytes at @c offset and compare against @c value. */
struct Instruction {
    Operator op;
    Size     size;
    uint16_t offset;  ///< Byte offset into packet buffer.
    uint64_t mask;    ///< MASK only: `(field & mask) == value`; unused otherwise.
    uint64_t value;   ///< Comparand, or expected masked value.
    int      regId = -1;  ///< Internal: assigned during load coalescing.
};

// ── Action ───────────────────────────────────────────────────────────────────

/** @brief The decision a matched sequence yields. */
enum class Action : uint8_t { PERMIT, DENY };

// ── Sequence ──────────────────────────────────────────────────────────────────

/** @brief A single rule: an ordered list of predicates (AND) plus an action. */
struct Sequence {
    std::vector<Instruction> preds;
    Action                   action = Action::PERMIT;

    static Sequence permit(std::vector<Instruction> p)
    { return { std::move(p), Action::PERMIT }; }

    static Sequence deny(std::vector<Instruction> p)
    { return { std::move(p), Action::DENY }; }
};

// ── Policy ────────────────────────────────────────────────────────────────────

/** @brief An ordered rule set; the first matching sequence wins, else @c defaultAction. */
struct Policy {
    std::vector<Sequence> sequences;
    Action                defaultAction = Action::DENY;
};

// ── Compiled output ───────────────────────────────────────────────────────────

/** @brief Compiled policy entry point: packet buffer in, PERMIT/DENY out. */
using PolicyFn = bool (*)(const uint8_t*) noexcept;

/**
 * @brief Move-only owner of one compiled policy's executable memory.
 *
 * Hands out the callable via operator() / getFn(); memory is freed by
 * destroy() on move-assign and destruction.
 */
class CompiledPolicy {
public:
    CompiledPolicy() = default;
    explicit CompiledPolicy(void* mem, std::size_t sz, PolicyFn fn)
        : mem(mem), sz(sz), fn(fn) {}

    CompiledPolicy(const CompiledPolicy&)            = delete;
    CompiledPolicy& operator=(const CompiledPolicy&) = delete;

    CompiledPolicy(CompiledPolicy&& o) noexcept
        : mem(o.mem), sz(o.sz), fn(o.fn)
    { o.mem = nullptr; o.sz = 0; o.fn = nullptr; }

    CompiledPolicy& operator=(CompiledPolicy&& o) noexcept {
        if (this != &o) {
            destroy();
            mem = o.mem; sz = o.sz; fn = o.fn;
            o.mem = nullptr; o.sz = 0; o.fn = nullptr;
        }
        return *this;
    }

    ~CompiledPolicy() { destroy(); }

    [[nodiscard]] bool valid()      const noexcept { return fn != nullptr; }
    bool operator()(const uint8_t* pkt) const noexcept { return fn(pkt); }
    PolicyFn    getFn()      const noexcept { return fn; }
    std::size_t code_size()  const noexcept { return sz; }

private:
    void destroy();
    void*       mem = nullptr;
    std::size_t sz  = 0;
    PolicyFn    fn  = nullptr;
};

// ── Entry points ──────────────────────────────────────────────────────────────

[[nodiscard]] CompiledPolicy compilePolicy(const Policy& policy);

[[nodiscard]] CompiledPolicy compilePolicy(
    std::span<const Sequence> program,
    Action defaultAction = Action::DENY);

[[nodiscard]] inline CompiledPolicy compilePolicy(
    const std::vector<Sequence>& program,
    Action defaultAction = Action::DENY)
{ return compilePolicy(std::span<const Sequence>(program), defaultAction); }

} // namespace policy::jit
