// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering Common.dll of Freelancer 1.1, for the
// macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// FlChecksum: the Freelancer 32-bit rolling checksum (Common.dll ?FlChecksum@@YAIPBXI@Z).

#include "fl_checksum.h"

COMMON_API unsigned int __cdecl FlChecksum(void const* Data, unsigned long Size)
{
    const unsigned char* Bytes = static_cast<const unsigned char*>(Data);
    unsigned long s1 = 0xFFFF;
    unsigned long s2 = 0xFFFF;

    unsigned long Remaining = Size;
    while (Remaining > 0)
    {
        // 0x10010330: chunk capped at 0x168; the final chunk is what is left.
        const unsigned long Chunk = (Remaining > 0x168) ? 0x168 : Remaining;
        for (unsigned long i = 0; i < Chunk; ++i)
        {
            // 0x1001035a: MOVSX byte -> s1 (>=0x80 counts negative); s2 folds s1
            // per byte. Both accumulators are plain 32-bit adds, no mask per byte.
            s1 += static_cast<signed char>(Bytes[i]);
            s2 += s1;
        }
        Bytes += Chunk;
        Remaining -= Chunk;
        // 0x10010380: 16-bit fold of BOTH accumulators after every chunk.
        s1 = (s1 & 0xFFFF) + (s1 >> 16);
        s2 = (s2 & 0xFFFF) + (s2 >> 16);
    }
    // 0x100103a9..0x100103cd: one final fold of both accumulators.
    s1 = (s1 & 0xFFFF) + (s1 >> 16);
    s2 = (s2 & 0xFFFF) + (s2 >> 16);
    return (s2 << 16) | (s1 & 0xFFFF);
}