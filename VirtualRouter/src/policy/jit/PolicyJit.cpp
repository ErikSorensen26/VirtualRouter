// PolicyJit.cpp

#include "PolicyJit.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__aarch64__)
    #define _ARM_ 1
    #define _X86_ 0
#elif defined(__x86_64__)
    #define _ARM_ 0
    #define _X86_ 1
#else
    #error "Architecture not supported"
#endif

namespace policy::jit
{
static bool probeMovbe() noexcept
{
#if _X86_
    uint32_t ecx = 0;
    __asm__ volatile("cpuid" : "=c"(ecx) : "a"(1) : "ebx", "edx");
    return (ecx >> 22) & 1;
#else
    return false;
#endif
}
static const bool kHasMovbe = probeMovbe();

static void* allocExec(size_t sz)
{
    void* p = ::mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        throw std::runtime_error(std::string("mmap failed: ") + strerror(errno));
    return p;
}

static void makeExec(void* p, size_t sz)
{
    if (::mprotect(p, sz, PROT_READ | PROT_EXEC) != 0)
    {
        ::munmap(p, sz);
        throw std::runtime_error(std::string("mprotect failed: ") + strerror(errno));
    }
}

static void freeExec(void* p, size_t sz) noexcept
{
    if (p && p != MAP_FAILED) ::munmap(p, sz);
}

void CompiledPolicy::destroy()
{
    freeExec(mem, sz);
}

struct Buffer
{
    std::vector<uint8_t> b;

    void u8(uint8_t x) { b.push_back(x); }
    void u32(uint32_t x) { for (int i = 0; i < 4; i++) { b.push_back(x & 0xFF); x >>= 8; } }
    void u64(uint64_t x) { for (int i = 0; i < 8; i++) { b.push_back(x & 0xFF); x >>= 8; } }

    std::size_t pos() const { return b.size(); }
    uint8_t* data() { return b.data(); }

    void patch32le(size_t p, int32_t v)
    {
        b[p + 0] = static_cast<uint8_t>(v);
        b[p + 1] = static_cast<uint8_t>(v >> 8);
        b[p + 2] = static_cast<uint8_t>(v >> 16);
        b[p + 3] = static_cast<uint8_t>(v >> 24);
    }

    // Patch a B.cond instruction: imm19 lives in bits[23:5]
    void patchBcond19(size_t p, int32_t instrDelta)
    {
        uint32_t cur;
        memcpy(&cur, b.data() + p, 4);
        cur = (cur & 0xFF00001Fu) | (static_cast<uint32_t>(instrDelta & 0x7FFFF) << 5);
        memcpy(b.data() + p, &cur, 4);
    }

    // Patch an unconditional B/BL: imm26 lives in bits[25:0]
    void patchB26(size_t p, int32_t instrDelta)
    {
        uint32_t cur;
        memcpy(&cur, b.data() + p, 4);
        cur = (cur & 0xFC000000u) | static_cast<uint32_t>(instrDelta & 0x03FFFFFF);
        memcpy(b.data() + p, &cur, 4);
    }
};

struct LoadKey
{
    uint16_t offset;
    Size size;
    bool operator<(const LoadKey& o) const
    {
        if (offset != o.offset) return offset < o.offset;
        return static_cast<uint8_t>(size) < static_cast<uint8_t>(o.size);
    }
    bool operator==(const LoadKey& o) const
    {
        return offset == o.offset && size == o.size;
    }
};

// ---------------------------------------------------------------------------
// x86-64 opcode constants
// ---------------------------------------------------------------------------

static constexpr uint8_t REX_W  = 0x48; // 64-bit operand size
static constexpr uint8_t REX_WB = 0x49; // 64-bit + REX.B (r/m extends to r8-r15)

#if _X86_

// Single-byte push/pop (non-extended registers)
static constexpr uint8_t PUSH_RBX = 0x53;
static constexpr uint8_t POP_RBX  = 0x5B;

// Extended push/pop (r12-r15) require REX.B prefix (0x41)
static constexpr uint8_t PUSH_R12 = 0x54;
static constexpr uint8_t PUSH_R13 = 0x55;
static constexpr uint8_t PUSH_R14 = 0x56;
static constexpr uint8_t PUSH_R15 = 0x57;
static constexpr uint8_t POP_R12  = 0x5C;
static constexpr uint8_t POP_R13  = 0x5D;
static constexpr uint8_t POP_R14  = 0x5E;
static constexpr uint8_t POP_R15  = 0x5F;

static constexpr uint8_t RET           = 0xC3;
static constexpr uint8_t MOV_EAX_IMM32 = 0xB8;
static constexpr uint8_t MOV_RAX_IMM64 = 0xB8; // with REX.W prefix
static constexpr uint8_t MOV_R_RM      = 0x8B; // MOV r64, r/m64
static constexpr uint8_t MOV_RM_R      = 0x89; // MOV r/m64, r64

// Two-byte escape prefix
static constexpr uint8_t ESC           = 0x0F;
static constexpr uint8_t P38           = 0x38; // after ESC: 0F 38 xx three-byte opcodes

// Conditional jumps (near, 32-bit relative; second byte after ESC)
static constexpr uint8_t JE_REL32  = 0x84; // ZF=1
static constexpr uint8_t JNE_REL32 = 0x85; // ZF=0
static constexpr uint8_t JBE_REL32 = 0x86; // CF=1 || ZF=1  (unsigned <=)
static constexpr uint8_t JAE_REL32 = 0x83; // CF=0          (unsigned >=)

// Zero-extend loads (after ESC)
static constexpr uint8_t MOVZX_RM8  = 0xB6; // MOVZX r64, r/m8
static constexpr uint8_t MOVZX_RM16 = 0xB7; // MOVZX r64, r/m16

// MOVBE (after ESC P38)
static constexpr uint8_t MOVBE_LOAD = 0xF0; // MOVBE r, m (byte-swapping load)

// BSWAP base (after ESC; +rd in low 3 bits)
static constexpr uint8_t BSWAP_BASE = 0xC8;

// Shift/rotate group (opcode byte 0xC1; ModRM encodes the direction)
static constexpr uint8_t SHIFT_IMM8 = 0xC1;
static constexpr uint8_t SHR_MODRM  = 0xE8; // mod=11, /5 (SHR), r/m=0 — OR with (enc&7)
static constexpr uint8_t ROL_MODRM  = 0xC0; // mod=11, /0 (ROL), r/m=0 — OR with (enc&7)

// Operand-size prefix (selects 16-bit operand)
static constexpr uint8_t OP16 = 0x66;

// REX extensions
static constexpr uint8_t REX_R  = 0x44; // REX.R only (reg field extends to r8-r15)
static constexpr uint8_t REX_B  = 0x41; // REX.B only (r/m or base field extends)

// Arithmetic: immediate forms (opcode 0x81; ModRM /x selects operation)
static constexpr uint8_t IMM32_OP  = 0x81;
static constexpr uint8_t CMP_RCX_MODRM = 0xF9; // mod=11, /7, r/m=rcx(1)
static constexpr uint8_t CMP_RDX_MODRM = 0xFA; // mod=11, /7, r/m=rdx(2)
static constexpr uint8_t AND_RDX_MODRM = 0xE2; // mod=11, /4, r/m=rdx(2)
static constexpr uint8_t SUB_RSP_MODRM = 0xEC; // mod=11, /5, r/m=rsp(4)
static constexpr uint8_t ADD_RSP_MODRM = 0xC4; // mod=11, /0, r/m=rsp(4)

// Register-to-register arithmetic
static constexpr uint8_t CMP_RM_R64 = 0x39; // CMP r/m64, r64
static constexpr uint8_t AND_RM_R64 = 0x21; // AND r/m64, r64

// MOV rdx, rcx (specific ModRM byte for REX.W 89 /r)
static constexpr uint8_t MODRM_MOV_RDX_RCX = 0xCA;

class Emitter
{
    Buffer& buf;

    // rdi = 7 (arg, pkt pointer)
    static constexpr int PKT_REG  = 7; // rdi
    static constexpr int SCRATCH  = 1; // rcx (compare target)
    static constexpr int SCRATCH2 = 2; // rdx (mask scratch)
    static constexpr int SCRATCH3 = 0; // rax (imm64 scratch / spill load)

    // Virtual reg 0-7 -> r8-r15 (enc 0-7, need REX.B when in r/m, REX.R when in reg)
    static constexpr int MAX_REGS = 8;

    struct Slot { bool inReg; int enc; int stkOff; };
    std::vector<Slot> slots;
    int nSlots    = 0;
    int spillBytes = 0;

    std::vector<size_t> curFixups;
    std::vector<size_t> permitFixups;
    std::vector<size_t> denyFixups;

public:
    explicit Emitter(Buffer& b) : buf(b) {}

    void allocSlots(int n)
    {
        nSlots = n;
        slots.resize(static_cast<size_t>(n));
        int spillIdx = 0;
        for (int i = 0; i < n; i++)
        {
            if (i < MAX_REGS)
            {
                slots[static_cast<size_t>(i)] = { true, i, 0 };
            }
            else
            {
                spillIdx++;
                slots[static_cast<size_t>(i)] = { false, 0, spillIdx * 8 };
            }
        }
        spillBytes = ((spillIdx * 8 + 8 + 15) & ~15);
    }

    // Only push callee-saved registers (r12-r15) for slots that actually use them.
    // Slots 0-3 → r8-r11 (caller-saved): no saves needed.
    // Slots 4-7 → r12-r15 (callee-saved): save only those allocated.
    void emitPrologue()
    {
        const int nc = std::max(0, std::min(nSlots, MAX_REGS) - 4);
        if (nc > 0) { buf.u8(REX_B); buf.u8(PUSH_R12); }
        if (nc > 1) { buf.u8(REX_B); buf.u8(PUSH_R13); }
        if (nc > 2) { buf.u8(REX_B); buf.u8(PUSH_R14); }
        if (nc > 3) { buf.u8(REX_B); buf.u8(PUSH_R15); }

        if (spillBytes > 0)
        {
            buf.u8(REX_W); buf.u8(IMM32_OP); buf.u8(SUB_RSP_MODRM);
            buf.u32(static_cast<uint32_t>(spillBytes));
        }
    }

    void emitEpilogue()
    {
        const int nc = std::max(0, std::min(nSlots, MAX_REGS) - 4);
        if (spillBytes > 0)
        {
            buf.u8(REX_W); buf.u8(IMM32_OP); buf.u8(ADD_RSP_MODRM);
            buf.u32(static_cast<uint32_t>(spillBytes));
        }
        if (nc > 3) { buf.u8(REX_B); buf.u8(POP_R15); }
        if (nc > 2) { buf.u8(REX_B); buf.u8(POP_R14); }
        if (nc > 1) { buf.u8(REX_B); buf.u8(POP_R13); }
        if (nc > 0) { buf.u8(REX_B); buf.u8(POP_R12); }
    }

    void emitReturn(bool action)
    {
        buf.u8(MOV_EAX_IMM32); buf.u32(action ? 1u : 0u);
        emitEpilogue();
        buf.u8(RET);
    }

    void emitLoad(int regId, uint16_t off, Size sz)
    {
        bool inReg  = slots[static_cast<size_t>(regId)].inReg;
        int dstEnc  = inReg ? slots[static_cast<size_t>(regId)].enc : SCRATCH3;
        bool dstExt = inReg;

        switch (sz)
        {
            case Size::S1: emitMovzx8(dstEnc, dstExt, off); break;
            case Size::S2: emitLoadBe16(dstEnc, dstExt, off); break;
            case Size::S3: emitLoadBe(dstEnc, dstExt, off, false);
                           emitShrImm8(dstEnc, dstExt, false, 8); break;
            case Size::S4: emitLoadBe(dstEnc, dstExt, off, false); break;
            case Size::S6: emitLoadBe(dstEnc, dstExt, off, true);
                           emitShrImm8(dstEnc, dstExt, true, 16); break;
            case Size::S8: emitLoadBe(dstEnc, dstExt, off, true); break;
        }

        if (!inReg)
        {
            int stk = slots[static_cast<size_t>(regId)].stkOff;
            buf.u8(REX_W); buf.u8(MOV_RM_R);
            buf.u8(0x84); buf.u8(0x24);
            buf.u32(static_cast<uint32_t>(stk));
        }
    }

    void loadToRcx(int regId)
    {
        if (slots[static_cast<size_t>(regId)].inReg)
        {
            int srcEnc = slots[static_cast<size_t>(regId)].enc;
            buf.u8(REX_WB); buf.u8(MOV_R_RM);
            buf.u8(static_cast<uint8_t>(0xC0 | (SCRATCH << 3) | (srcEnc & 7)));
        }
        else
        {
            int stk = slots[static_cast<size_t>(regId)].stkOff;
            buf.u8(REX_W); buf.u8(MOV_R_RM);
            buf.u8(0x8C); buf.u8(0x24);
            buf.u32(static_cast<uint32_t>(stk));
        }
    }

    void beginSequence()
    {
        curFixups.clear();
    }

    void emitPred(const Instruction& p)
    {
        loadToRcx(p.regId);
        switch (p.op)
        {
            case Operator::EQ:
            {
                emitCmpRcxImm(p.value);
                emitJccFail(JNE_REL32);
                break;
            }
            case Operator::NEQ:
            {
                emitCmpRcxImm(p.value);
                emitJccFail(JE_REL32);
                break;
            }
            case Operator::GT:
            {
                emitCmpRcxImm(p.value);
                emitJccFail(JBE_REL32);
                break;
            }
            case Operator::LT:
            {
                emitCmpRcxImm(p.value);
                emitJccFail(JAE_REL32);
                break;
            }
            case Operator::MASK:
            {
                buf.u8(REX_W); buf.u8(MOV_RM_R); buf.u8(MODRM_MOV_RDX_RCX);
                emitAndRdxImm(p.mask);
                emitCmpRdxImm(p.value);
                emitJccFail(JNE_REL32);
                break;
            }
        }
    }

    void resolveSequenceFails()
    {
        size_t target = buf.pos();
        for (size_t fix : curFixups)
        {
            int32_t delta = static_cast<int32_t>(target - (fix + 4));
            buf.patch32le(fix, delta);
        }
        curFixups.clear();
    }

    // Jump to one of the two shared epilogue blocks emitted by emitSharedEpilogues().
    void emitJmpToEpilogue(bool action)
    {
        static constexpr uint8_t JMP_REL32 = 0xE9;
        buf.u8(JMP_REL32);
        if (action) permitFixups.push_back(buf.pos());
        else        denyFixups.push_back(buf.pos());
        buf.u32(0); // patched by emitSharedEpilogues
    }

    // Emit one PERMIT and one DENY epilogue block at the current position, then
    // patch every outstanding jmp-to-epilogue fixup to point at the right one.
    void emitSharedEpilogues()
    {
        size_t permitPos = buf.pos();
        buf.u8(MOV_EAX_IMM32); buf.u32(1u);
        emitEpilogue(); buf.u8(RET);

        size_t denyPos = buf.pos();
        buf.u8(MOV_EAX_IMM32); buf.u32(0u);
        emitEpilogue(); buf.u8(RET);

        for (size_t fix : permitFixups)
            buf.patch32le(fix, static_cast<int32_t>(permitPos - (fix + 4)));
        for (size_t fix : denyFixups)
            buf.patch32le(fix, static_cast<int32_t>(denyPos - (fix + 4)));
    }

    // Load field at (off, sz) into rcx, then emit the 256-entry byte action
    // table lookup.  No jumps inside the sequence scan at all.
    // Requires allocSlots(1) + emitPrologue() to have been called already.
    void emitJumpTable(uint16_t off, Size sz, Action defaultAction,
                       const std::vector<Sequence>& seqs)
    {
        emitLoad(0, off, sz);
        loadToRcx(0);

        // lea rdx, [rip + <delta_to_table>]  —  REX.W 8D 15 imm32  (7 bytes total)
        size_t leaPos = buf.pos();
        buf.u8(REX_W); buf.u8(0x8D); buf.u8(0x15); buf.u32(0);

        // movzx eax, byte [rdx + rcx]  —  0F B6 04 0A
        // ModRM 0x04 = mod=00, reg=eax(0), rm=SIB
        // SIB   0x0A = scale=1, index=rcx(1), base=rdx(2)
        buf.u8(ESC); buf.u8(MOVZX_RM8); buf.u8(0x04); buf.u8(0x0A);

        emitEpilogue();
        buf.u8(RET);

        // Pad to the next 64-byte boundary so the table occupies exactly 4
        // cache lines and never shares a line with the preceding code.
        while (buf.pos() % 64 != 0) buf.u8(0x90); // NOP padding

        // Patch LEA: RIP at end of LEA instruction = leaPos + 7
        size_t tablePos = buf.pos();
        buf.patch32le(leaPos + 3, static_cast<int32_t>(tablePos - (leaPos + 7)));

        // 256-byte action table: entry v = 1 (PERMIT) or 0 (DENY)
        const uint8_t def = defaultAction == Action::PERMIT ? 1u : 0u;
        for (int v = 0; v < 256; v++)
        {
            uint8_t act = def;
            for (const Sequence& s : seqs)
                if (s.preds[0].value == static_cast<uint64_t>(v))
                { act = s.action == Action::PERMIT ? 1u : 0u; break; }
            buf.u8(act);
        }
    }

private:
    uint8_t rexWr(bool dstExt, bool srcExt = false)
    {
        // REX.W always; REX.R extends the reg field; REX.B extends r/m
        return static_cast<uint8_t>(REX_W | (dstExt ? 0x04 : 0) | (srcExt ? 0x01 : 0));
    }

    void emitModrmRdi(int dstEnc, uint16_t off)
    {
        if (off < 128)
        {
            buf.u8(static_cast<uint8_t>(0x40 | ((dstEnc & 7) << 3) | PKT_REG));
            buf.u8(static_cast<uint8_t>(off));
        }
        else
        {
            buf.u8(static_cast<uint8_t>(0x80 | ((dstEnc & 7) << 3) | PKT_REG));
            buf.u32(static_cast<uint32_t>(off));
        }
    }

    void emitMovzx8(int dstEnc, bool dstExt, uint16_t off)
    {
        buf.u8(rexWr(dstExt));
        buf.u8(ESC); buf.u8(MOVZX_RM8);
        emitModrmRdi(dstEnc, off);
    }

    void emitLoadBe16(int dstEnc, bool dstExt, uint16_t off)
    {
        if (kHasMovbe)
        {
            buf.u8(OP16);
            if (dstExt) buf.u8(REX_R);
            buf.u8(ESC); buf.u8(P38); buf.u8(MOVBE_LOAD);
            emitModrmRdi(dstEnc, off);
            // MOVZX r64, r16 (zero-extend the 16-bit result)
            buf.u8(static_cast<uint8_t>(REX_W | (dstExt ? 0x05 : 0))); // REX.W + REX.R + REX.B
            buf.u8(ESC); buf.u8(MOVZX_RM16);
            buf.u8(static_cast<uint8_t>(0xC0 | ((dstEnc & 7) << 3) | (dstEnc & 7)));
        }
        else
        {
            // Load 16-bit little-endian, zero-extend, then byte-swap the low 16
            buf.u8(rexWr(dstExt));
            buf.u8(ESC); buf.u8(MOVZX_RM16);
            emitModrmRdi(dstEnc, off);
            // ROL r16, 8
            buf.u8(OP16);
            if (dstExt) buf.u8(REX_B);
            buf.u8(SHIFT_IMM8);
            buf.u8(static_cast<uint8_t>(ROL_MODRM | (dstEnc & 7)));
            buf.u8(8);
        }
    }

    void emitLoadBe(int dstEnc, bool dstExt, uint16_t off, bool w64)
    {
        if (kHasMovbe)
        {
            if (w64) buf.u8(rexWr(dstExt));
            else if (dstExt) buf.u8(REX_R);
            buf.u8(ESC); buf.u8(P38); buf.u8(MOVBE_LOAD);
            emitModrmRdi(dstEnc, off);
        }
        else
        {
            if (w64) buf.u8(rexWr(dstExt));
            else if (dstExt) buf.u8(REX_R);
            buf.u8(MOV_R_RM);
            emitModrmRdi(dstEnc, off);
            // BSWAP: REX 0F C8+enc
            if (w64) buf.u8(dstExt ? REX_WB : REX_W);
            else if (dstExt) buf.u8(REX_B);
            buf.u8(ESC); buf.u8(static_cast<uint8_t>(BSWAP_BASE | (dstEnc & 7)));
        }
    }

    void emitShrImm8(int enc, bool ext, bool w64, uint8_t imm)
    {
        if (w64) buf.u8(ext ? REX_WB : REX_W);
        else if (ext) buf.u8(REX_B);
        buf.u8(SHIFT_IMM8);
        buf.u8(static_cast<uint8_t>(SHR_MODRM | (enc & 7)));
        buf.u8(imm);
    }

    void emitCmpRcxImm(uint64_t v)
    {
        if (v <= 0x7FFFFFFFu)
        {
            buf.u8(REX_W); buf.u8(IMM32_OP); buf.u8(CMP_RCX_MODRM);
            buf.u32(static_cast<uint32_t>(v));
        }
        else
        {
            // mov rax, imm64; cmp rcx, rax
            buf.u8(REX_W); buf.u8(MOV_RAX_IMM64); buf.u64(v);
            buf.u8(REX_W); buf.u8(CMP_RM_R64); buf.u8(0xC1);
        }
    }

    void emitAndRdxImm(uint64_t m)
    {
        if (m <= 0x7FFFFFFFu)
        {
            buf.u8(REX_W); buf.u8(IMM32_OP); buf.u8(AND_RDX_MODRM);
            buf.u32(static_cast<uint32_t>(m));
        }
        else
        {
            buf.u8(REX_W); buf.u8(MOV_RAX_IMM64); buf.u64(m);
            buf.u8(REX_W); buf.u8(AND_RM_R64); buf.u8(0xC2); // and rdx, rax
        }
    }

    void emitCmpRdxImm(uint64_t v)
    {
        if (v <= 0x7FFFFFFFu)
        {
            buf.u8(REX_W); buf.u8(IMM32_OP); buf.u8(CMP_RDX_MODRM);
            buf.u32(static_cast<uint32_t>(v));
        }
        else
        {
            buf.u8(REX_W); buf.u8(MOV_RAX_IMM64); buf.u64(v);
            buf.u8(REX_W); buf.u8(CMP_RM_R64); buf.u8(0xC2); // cmp rdx, rax
        }
    }

    void emitJccFail(uint8_t jccOpcode)
    {
        buf.u8(ESC); buf.u8(jccOpcode);
        curFixups.push_back(buf.pos());
        buf.u32(0); // placeholder, patched by resolveSequenceFails
    }
};

#elif _ARM_

// ---------------------------------------------------------------------------
// AArch64 instruction constants
// ---------------------------------------------------------------------------

// B.cond condition codes (bits[3:0] of the B.cond encoding)
static constexpr uint8_t COND_EQ = 0x00; // Equal (Z=1)
static constexpr uint8_t COND_NE = 0x01; // Not equal (Z=0)
static constexpr uint8_t COND_HS = 0x02; // Unsigned >= (C=1)  [also CS]
static constexpr uint8_t COND_LO = 0x03; // Unsigned <  (C=0)  [also CC]
static constexpr uint8_t COND_LS = 0x09; // Unsigned <= (C=0 || Z=1)

// Fixed instruction encodings
static constexpr uint32_t MOVZ_W0_0  = 0x52800000u; // MOVZ W0, #0
static constexpr uint32_t MOVZ_W0_1  = 0x52800020u; // MOVZ W0, #1
static constexpr uint32_t MOV_X29_SP = 0x910003FDu; // ADD x29, sp, #0 (MOV x29, sp)
static constexpr uint32_t RET_LR     = 0xD65F03C0u; // RET (branches to x30)

class Emitter
{
    Buffer& buf;

    static constexpr int PKT_REG  = 0;   // x0
    static constexpr int SCRATCH1 = 9;   // x9
    static constexpr int SCRATCH2 = 10;  // x10
    static constexpr int SCRATCH3 = 11;  // x11
    static constexpr int LOAD_BASE = 19; // x19-x26 for virtual regs 0-7
    static constexpr int MAX_REGS  = 8;

    struct Slot { bool inReg; int reg; };
    std::vector<Slot> slots;
    std::vector<size_t> curFixups; // byte positions of B.cond instructions to patch
    std::vector<size_t> permitFixups;
    std::vector<size_t> denyFixups;

public:
    explicit Emitter(Buffer& b) : buf(b) {}

    void allocSlots(int n)
    {
        slots.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; i++)
        {
            slots[static_cast<size_t>(i)] = (i < MAX_REGS)
                ? Slot{true, LOAD_BASE + i}
                : Slot{false, SCRATCH1};
        }
    }

    void emitPrologue()
    {
        // Allocate 80-byte frame, save frame pointer and link register
        emitStpPreindex(29, 30, 31, -80);
        emitStpOffset(19, 20, 31, 16);
        emitStpOffset(21, 22, 31, 32);
        emitStpOffset(23, 24, 31, 48);
        emitStpOffset(25, 26, 31, 64);
        buf.u32(MOV_X29_SP);
    }

    void emitEpilogueAndRet()
    {
        emitLdpOffset(25, 26, 31, 64);
        emitLdpOffset(23, 24, 31, 48);
        emitLdpOffset(21, 22, 31, 32);
        emitLdpOffset(19, 20, 31, 16);
        emitLdpPostindex(29, 30, 31, 80); // ldp x29, x30, [sp], #80
        buf.u32(RET_LR);
    }

    void emitReturn(bool action)
    {
        buf.u32(action ? MOVZ_W0_1 : MOVZ_W0_0);
        emitEpilogueAndRet();
    }

    void emitLoad(int regId, uint16_t off, Size sz)
    {
        int dst = slots[static_cast<size_t>(regId)].inReg
            ? slots[static_cast<size_t>(regId)].reg
            : SCRATCH1;

        switch (sz)
        {
            case Size::S1: emitLdrb(dst, PKT_REG, off); break;
            case Size::S2: emitLdrh(dst, PKT_REG, off); emitRev16W(dst); break;
            case Size::S3: emitLdr32(dst, PKT_REG, off);
                           emitRevW(dst);
                           emitLsr32Imm(dst, dst, 8); break;
            case Size::S4: emitLdr32(dst, PKT_REG, off); emitRevW(dst); break;
            case Size::S6: emitLdr64(dst, PKT_REG, off);
                           emitRevX(dst);
                           emitLsr64Imm(dst, dst, 16); break;
            case Size::S8: emitLdr64(dst, PKT_REG, off); emitRevX(dst); break;
        }
    }

    int getReg(int regId, uint16_t off, Size sz)
    {
        if (slots[static_cast<size_t>(regId)].inReg)
            return slots[static_cast<size_t>(regId)].reg;
        emitLoad(regId, off, sz);
        return SCRATCH1;
    }

    void beginSequence()
    {
        curFixups.clear();
    }

    void emitPred(const Instruction& p)
    {
        int valReg = getReg(p.regId, p.offset, p.size);

        switch (p.op)
        {
            case Operator::EQ:
            {
                int imm = loadImm64(SCRATCH2, p.value);
                emitSubsXzr(valReg, imm);
                emitBcondFail(COND_NE);
                break;
            }
            case Operator::NEQ:
            {
                int imm = loadImm64(SCRATCH2, p.value);
                emitSubsXzr(valReg, imm);
                emitBcondFail(COND_EQ);
                break;
            }
            case Operator::GT:
            {
                // unsigned >: fail if val <= imm  (B.LS)
                int imm = loadImm64(SCRATCH2, p.value);
                emitSubsXzr(valReg, imm);
                emitBcondFail(COND_LS);
                break;
            }
            case Operator::LT:
            {
                // unsigned <: fail if val >= imm  (B.HS)
                int imm = loadImm64(SCRATCH2, p.value);
                emitSubsXzr(valReg, imm);
                emitBcondFail(COND_HS);
                break;
            }
            case Operator::MASK:
            {
                int maskR = loadImm64(SCRATCH2, p.mask);
                emitAnd(SCRATCH3, valReg, maskR);
                int val2R = loadImm64(SCRATCH2, p.value);
                emitSubsXzr(SCRATCH3, val2R);
                emitBcondFail(COND_NE);
                break;
            }
        }
    }

    void resolveSequenceFails()
    {
        size_t target = buf.pos();
        for (size_t fix : curFixups)
        {
            int32_t delta = static_cast<int32_t>((target - fix) / 4);
            buf.patchBcond19(fix, delta);
        }
        curFixups.clear();
    }

    void emitJmpToEpilogue(bool action)
    {
        // Unconditional B with 26-bit offset placeholder
        if (action) permitFixups.push_back(buf.pos());
        else        denyFixups.push_back(buf.pos());
        buf.u32(0x14000000u); // B +0 placeholder
    }

    void emitSharedEpilogues()
    {
        size_t permitPos = buf.pos();
        buf.u32(MOVZ_W0_1);
        emitEpilogueAndRet();

        size_t denyPos = buf.pos();
        buf.u32(MOVZ_W0_0);
        emitEpilogueAndRet();

        for (size_t fix : permitFixups)
            buf.patchB26(fix, static_cast<int32_t>((permitPos - fix) / 4));
        for (size_t fix : denyFixups)
            buf.patchB26(fix, static_cast<int32_t>((denyPos - fix) / 4));
    }

private:

    int loadImm64(int scratch, uint64_t v)
    {
        emitMovz64(scratch, static_cast<uint16_t>(v), 0);
        if (v > 0xFFFFu)         emitMovk64(scratch, static_cast<uint16_t>(v >> 16), 16);
        if (v > 0xFFFFFFFFu)     emitMovk64(scratch, static_cast<uint16_t>(v >> 32), 32);
        if (v > 0xFFFFFFFFFFFFu) emitMovk64(scratch, static_cast<uint16_t>(v >> 48), 48);
        return scratch;
    }

    // LDRB Wt, [Xn, #imm] — imm12 = byte offset (unscaled)
    void emitLdrb(int rt, int rn, uint16_t off)
    {
        buf.u32(0x39400000u | (static_cast<uint32_t>(off) << 10)
            | (static_cast<uint32_t>(rn) << 5) | rt);
    }
    // LDRH Wt, [Xn, #imm] — imm12 = halfword offset (scaled by 2)
    void emitLdrh(int rt, int rn, uint16_t off)
    {
        buf.u32(0x79400000u | (static_cast<uint32_t>(off / 2) << 10)
            | (static_cast<uint32_t>(rn) << 5) | rt);
    }
    // LDR Wt, [Xn, #imm] — imm12 = word offset (scaled by 4)
    void emitLdr32(int rt, int rn, uint16_t off)
    {
        buf.u32(0xB9400000u | (static_cast<uint32_t>(off / 4) << 10)
            | (static_cast<uint32_t>(rn) << 5) | rt);
    }
    // LDR Xt, [Xn, #imm] — imm12 = doubleword offset (scaled by 8)
    void emitLdr64(int rt, int rn, uint16_t off)
    {
        buf.u32(0xF9400000u | (static_cast<uint32_t>(off / 8) << 10)
            | (static_cast<uint32_t>(rn) << 5) | rt);
    }

    // REV16 Wt, Wt — byte-reverse within each 16-bit halfword
    void emitRev16W(int rt)
    {
        buf.u32(0x5AC00400u | (static_cast<uint32_t>(rt) << 5) | rt);
    }
    // REV Wt, Wt — byte-reverse 32-bit word
    void emitRevW(int rt)
    {
        buf.u32(0x5AC00800u | (static_cast<uint32_t>(rt) << 5) | rt);
    }
    // REV Xt, Xt — byte-reverse 64-bit doubleword
    void emitRevX(int rt)
    {
        buf.u32(0xDAC00C00u | (static_cast<uint32_t>(rt) << 5) | rt);
    }

    // LSR Wd, Wn, #imm (UBFM Wd, Wn, #imm, #31)
    void emitLsr32Imm(int rd, int rn, int imm)
    {
        buf.u32(0x53000000u | (static_cast<uint32_t>(imm) << 16)
            | (31u << 10) | (static_cast<uint32_t>(rn) << 5) | rd);
    }
    // LSR Xd, Xn, #imm (UBFM Xd, Xn, #imm, #63)
    void emitLsr64Imm(int rd, int rn, int imm)
    {
        buf.u32(0xD3400000u | (static_cast<uint32_t>(imm) << 16)
            | (63u << 10) | (static_cast<uint32_t>(rn) << 5) | rd);
    }

    // MOVZ Xd, #imm16, LSL #shift
    void emitMovz64(int rd, uint16_t imm, int shift)
    {
        buf.u32(0xD2800000u | (static_cast<uint32_t>(shift / 16) << 21)
            | (static_cast<uint32_t>(imm) << 5) | rd);
    }
    // MOVK Xd, #imm16, LSL #shift
    void emitMovk64(int rd, uint16_t imm, int shift)
    {
        buf.u32(0xF2800000u | (static_cast<uint32_t>(shift / 16) << 21)
            | (static_cast<uint32_t>(imm) << 5) | rd);
    }

    // SUBS XZR, Xn, Xm — compare and set flags, discard result
    void emitSubsXzr(int rn, int rm)
    {
        buf.u32(0xEB000000u | (static_cast<uint32_t>(rm) << 16)
            | (static_cast<uint32_t>(rn) << 5) | 31u);
    }

    // AND Xd, Xn, Xm
    void emitAnd(int rd, int rn, int rm)
    {
        buf.u32(0x8A000000u | (static_cast<uint32_t>(rm) << 16)
            | (static_cast<uint32_t>(rn) << 5) | rd);
    }

    // B.cond with placeholder offset (patched later by resolveSequenceFails)
    void emitBcondFail(uint8_t cond)
    {
        curFixups.push_back(buf.pos());
        buf.u32(0x54000000u | cond); // imm19=0 placeholder
    }

    // STP Xt1, Xt2, [Xn, #imm]! — pre-index (imm in bytes, signed)
    void emitStpPreindex(int r1, int r2, int rn, int immBytes)
    {
        int imm7 = immBytes / 8;
        buf.u32(0xA9800000u | (static_cast<uint32_t>(imm7 & 0x7F) << 15)
            | (static_cast<uint32_t>(r2) << 10) | (static_cast<uint32_t>(rn) << 5)
            | static_cast<uint32_t>(r1));
    }
    // STP Xt1, Xt2, [Xn, #imm] — signed offset
    void emitStpOffset(int r1, int r2, int rn, int immBytes)
    {
        int imm7 = immBytes / 8;
        buf.u32(0xA9000000u | (static_cast<uint32_t>(imm7 & 0x7F) << 15)
            | (static_cast<uint32_t>(r2) << 10) | (static_cast<uint32_t>(rn) << 5)
            | static_cast<uint32_t>(r1));
    }
    // LDP Xt1, Xt2, [Xn, #imm] — signed offset
    void emitLdpOffset(int r1, int r2, int rn, int immBytes)
    {
        int imm7 = immBytes / 8;
        buf.u32(0xA9400000u | (static_cast<uint32_t>(imm7 & 0x7F) << 15)
            | (static_cast<uint32_t>(r2) << 10) | (static_cast<uint32_t>(rn) << 5)
            | static_cast<uint32_t>(r1));
    }
    // LDP Xt1, Xt2, [Xn], #imm — post-index
    void emitLdpPostindex(int r1, int r2, int rn, int immBytes)
    {
        int imm7 = immBytes / 8;
        buf.u32(0xA8C00000u | (static_cast<uint32_t>(imm7 & 0x7F) << 15)
            | (static_cast<uint32_t>(r2) << 10) | (static_cast<uint32_t>(rn) << 5)
            | static_cast<uint32_t>(r1));
    }
};

#endif

// ---------------------------------------------------------------------------
// Policy compiler
// ---------------------------------------------------------------------------

// Returns true when every sequence has exactly one EQ predicate on the same
// (offset, size) field and every value fits in a byte (table stays ≤ 256 B).
static bool isJumpTablePolicy(const Policy& policy)
{
    if (policy.sequences.empty()) return false;
    const uint16_t off0 = policy.sequences[0].preds[0].offset;
    const Size     sz0  = policy.sequences[0].preds[0].size;
    for (const Sequence& seq : policy.sequences)
    {
        if (seq.preds.size() != 1) return false;
        const Instruction& p = seq.preds[0];
        if (p.op != Operator::EQ) return false;
        if (p.offset != off0 || p.size != sz0) return false;
        if (p.value > 255) return false;
    }
    return true;
}

[[nodiscard]] CompiledPolicy compilePolicy(const Policy& policy)
{
    Buffer buf;

    // --- trivial: no sequences ---
    if (policy.sequences.empty())
    {
        bool v = policy.defaultAction == Action::PERMIT;
#if _ARM_
        buf.u32(v ? MOVZ_W0_1 : MOVZ_W0_0);
        buf.u32(RET_LR);
#elif _X86_
        buf.u8(MOV_EAX_IMM32); buf.u32(v ? 1u : 0u);
        buf.u8(RET);
#endif
        size_t sz = buf.pos();
        void* mem = allocExec(sz);
        memcpy(mem, buf.data(), sz);
        makeExec(mem, sz);
        return CompiledPolicy(mem, sz, reinterpret_cast<PolicyFn>(mem));
    }

#if _X86_
    // --- jump-table path: O(1) dispatch, zero per-sequence branches ---
    if (isJumpTablePolicy(policy))
    {
        Emitter em(buf);
        em.allocSlots(1);
        em.emitPrologue();
        em.emitJumpTable(policy.sequences[0].preds[0].offset,
                         policy.sequences[0].preds[0].size,
                         policy.defaultAction, policy.sequences);
        size_t sz = buf.pos();
        void* mem = allocExec(sz);
        memcpy(mem, buf.data(), sz);
        makeExec(mem, sz);
        return CompiledPolicy(mem, sz, reinterpret_cast<PolicyFn>(mem));
    }
#endif

    // --- linear path with inter-sequence load reuse + shared epilogues ---

    // Pre-pass: assign a persistent register slot to every unique (offset, size)
    // pair.  Use a sorted vector instead of std::map to avoid pointer-chasing.
    std::vector<std::pair<LoadKey, int>> loadVec;
    loadVec.reserve(32);
    auto findSlot = [&](LoadKey k) -> int {
        auto it = std::lower_bound(loadVec.begin(), loadVec.end(), k,
            [](const std::pair<LoadKey,int>& a, const LoadKey& b){ return a.first < b; });
        if (it != loadVec.end() && it->first == k) return it->second;
        int id = static_cast<int>(loadVec.size());
        loadVec.insert(it, {k, id});
        return id;
    };
    for (const Sequence& seq : policy.sequences)
        for (const Instruction& p : seq.preds)
            findSlot({p.offset, p.size});

    Emitter em(buf);
    em.allocSlots(static_cast<int>(loadVec.size()));
    em.emitPrologue();

    // Lazy inter-sequence reuse: load each field on first use, then leave it
    // in its register for all subsequent sequences.  Fields that are never
    // reached (e.g. seq[3]'s field when most packets match seq[0]) are never
    // loaded, unlike the aggressive up-front hoisting approach.
    std::vector<bool> loaded(loadVec.size(), false);

    for (const Sequence& seq : policy.sequences)
    {
        em.beginSequence();

        for (const Instruction& p : seq.preds)
        {
            int regId = findSlot({p.offset, p.size});
            if (!loaded[static_cast<size_t>(regId)])
            {
                em.emitLoad(regId, p.offset, p.size);
                loaded[static_cast<size_t>(regId)] = true;
            }
            Instruction pi = p;
            pi.regId = regId;
            em.emitPred(pi);
        }

        // All predicates passed — jump to the shared epilogue for this action
        em.emitJmpToEpilogue(seq.action == Action::PERMIT);
        // Patch all fail branches to land at the start of the next sequence
        em.resolveSequenceFails();
    }

    // Default action falls through to here
    em.emitJmpToEpilogue(policy.defaultAction == Action::PERMIT);
    // Emit one PERMIT epilogue and one DENY epilogue; patch all jmp fixups
    em.emitSharedEpilogues();

    size_t sz = buf.pos();
    void* mem = allocExec(sz);
    memcpy(mem, buf.data(), sz);
    makeExec(mem, sz);

#if _ARM_
    __builtin___clear_cache(static_cast<char*>(mem), static_cast<char*>(mem) + sz);
#endif

    return CompiledPolicy(mem, sz, reinterpret_cast<PolicyFn>(mem));
}

[[nodiscard]] CompiledPolicy compilePolicy(std::span<const Sequence> program, Action action)
{
    Policy p;
    p.sequences.assign(program.begin(), program.end());
    p.defaultAction = action;
    return compilePolicy(p);
}

}
