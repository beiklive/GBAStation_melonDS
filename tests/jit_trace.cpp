// Exercise the real trace builder and Thumb interpreter without emitting host code.
#include "ARMJIT_Internal.h"
#include <algorithm>
#include <cstdlib>
#include <initializer_list>

#define ARMJIT_COMPILER_H
namespace ARMJIT {
class Compiler {
public:
    int emittedCount = 0;
    bool CanCompile(bool, u16) { return true; }
    JitBlockEntry CompileBlock(ARM*, bool, FetchedInstr*, int count, bool) { emittedCount = count; return nullptr; }
    u32 SubEntryOffset(JitBlockEntry) { return 0; }
    JitBlockEntry AddEntryOffset(u32) { return nullptr; }
    void Reset() {}
};
extern Compiler* JITCompiler;
}
#include "ARMJIT.cpp"

namespace {
constexpr u32 base = 0x02000000;
u32 executableEnd;
u16 program[16];
u16 readCode(u32 addr) { return program[(addr - base) / 2]; }
}

namespace Config { int JIT_MaxBlockSize, JIT_BranchOptimisations, JIT_LiteralOptimisations; }
namespace NDS {
u8 ARM7MemTimings[0x20000][4];
void NocashPrint(u32, u32) { std::abort(); }
}
namespace ARMJIT_Memory {
int ClassifyAddress7(u32 addr) { return addr >= base && addr < executableEnd ? memregion_MainRAM : memregion_Other; }
int ClassifyAddress9(u32 addr) { return ClassifyAddress7(addr); }
u32 LocaliseAddress(int region, u32, u32 addr) { return (u32(region) << 27) | (addr - base); }
void SetCodeProtection(int, u32, bool) {}
}

u32 ARM::ConditionTable[16];
ARM::ARM(u32 num) : Num(num) {}
ARM::~ARM() {}
void ARM::Reset() {}
void ARM::DoSavestate(Savestate*) {}
void ARM::RestoreCPSR() { std::abort(); }
void ARM::UpdateMode(u32, u32) { std::abort(); }
u32 ARMv5::CodeRead32(u32, bool) { std::abort(); }
u32 ARMv5::CP15Read(u32) { std::abort(); }
void ARMv5::CP15Write(u32, u32) { std::abort(); }

ARMv4::ARMv4() : ARM(1) {
    BusRead16 = readCode;
    BusRead32 = [](u32) { return 123u; };
}
void ARMv4::Reset() {}
void ARMv4::FillPipeline() {
    NextInstr[0] = BusRead16(R[15]-2);
    NextInstr[1] = BusRead16(R[15]);
}
void ARMv4::JumpTo(u32 addr, bool) {
    R[15] = (addr & ~1u) + 2;
    FillPipeline();
}
void ARMv4::Execute() { std::abort(); }
void ARMv4::ExecuteJIT() { std::abort(); }

int main() {
    using namespace ARMJIT;
    Compiler backend;
    JITCompiler = &backend;
    ARMv4 cpu;
    Config::JIT_BranchOptimisations = Config::JIT_LiteralOptimisations = 1;
    auto trace = [&](int budget, u32 executableBytes, std::initializer_list<u16> opcodes) {
        std::fill_n(program, 16, 0x2000); // mov r0, #0
        std::copy(opcodes.begin(), opcodes.end(), program);
        executableEnd = base + executableBytes;
        Config::JIT_MaxBlockSize = budget;
        cpu.R[15] = base + 2;
        cpu.CPSR = 0x20;
        cpu.Cycles = cpu.DataCycles = cpu.DataRegion = cpu.CodeCycles = 0;
        cpu.IRQ = cpu.Halted = 0;
        CompileBlock(&cpu);
    };

    // First or subsequent instruction is readable, but has no code index.
    trace(2, 0, {0x2007});
    assert(cpu.R[0] == 7 && cpu.R[15] == base + 4 && JitBlocks7.empty());
    trace(2, 2, {0x2001, 0x2002});
    assert(cpu.R[0] == 2 && cpu.R[15] == base + 6 && JitBlocks7.empty());

    // A single LDR references a literal outside its own 512-byte range.
    trace(1, 4096, {0x48ff});
    assert(cpu.R[0] == 123 && JitBlocks7.at(base)->NumAddresses == 2);
    InvalidateByAddr(LocaliseCodeAddress(1, base));
    // A four-halfword block ending in BL emits just three backend instructions.
    trace(4, 4096, {0x2001, 0x2002, 0xf000, 0xf801});
    assert(backend.emittedCount == 3 && cpu.R[0] == 2 && cpu.R[15] == base + 12);
    assert(JitBlocks7.count(base) == 1);
    puts("JIT traces: uncached execution, literal ranges and Thumb block budget passed");
}
