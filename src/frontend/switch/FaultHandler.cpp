#include <switch.h>
#include <stdio.h>
#include <inttypes.h>

extern char __start__;

void HandleFault(u64 pc, u64 lr, u64 fp, u64 faultAddr, u32 desc)
{
    FILE* log = fopen("/switch/melonds/melonds_gl.log", "ab");
    if (!log) log = fopen("melonds_gl.log", "ab");
    if (log)
    {
        // The frame pointer may be corrupt. Save the registers and load base
        // without dereferencing the faulting thread's stack.
        fprintf(log, "FAULT pc=%#" PRIx64 " lr=%#" PRIx64 " fp=%#" PRIx64
                " addr=%#" PRIx64 " desc=%u base=%p\n",
                pc, lr, fp, faultAddr, desc, (void*)&__start__);
        fclose(log);
    }
}
