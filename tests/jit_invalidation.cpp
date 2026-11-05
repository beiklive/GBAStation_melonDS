// Host test for the real JIT block tables; only page protection is stubbed.
#include "ARMJIT.cpp"
namespace ARMJIT_Memory { void SetCodeProtection(int, u32, bool) {} }
int main() {
    using namespace ARMJIT;
    constexpr u32 base = u32(ARMJIT_Memory::memregion_MainRAM) << 27;
    AddressRange& r = CodeIndexMainRAM[0];
    auto add = [&](unsigned offset) {
        auto* block = new JitBlock(0,0,1,1);
        block->StartAddr=0x02000000 + offset;
        block->StartAddrLocal = base+offset;
        block->AddressRanges()[0]=base;
        block->AddressMasks()[0]=1u<<(offset/16);
        block->Literals()[0]=base+offset;
        r.Blocks.Add(block);r.Code|=block->AddressMasks()[0];JitBlocks9[block->StartAddr]=block;
    };
    add(0); add(16);
    InvalidateByAddr(base+1);
    assert(r.Blocks.Length==1 && r.Code==2 && InvalidLiterals.Find(base)!=-1);
    add(0);InvalidateByAddr(base);
    assert(r.Blocks.Length==1 && InvalidLiterals.Length==1 && JitBlocks9.size()==1);
    InvalidateByAddr(base+17);
    assert(r.Blocks.Length==0 && InvalidLiterals.Length==2 && JitBlocks9.empty());
    puts("JIT invalidation: neighbours preserved, byte writes detected, no duplicate exclusions");
}
