// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering Common.dll of Freelancer 1.1, for the
// macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

#pragma once

#include "common_exports.h"

#include <cstddef>

// The Freelancer 32-bit rolling checksum: two 16-bit one-sum folders folded in
// chunks of 0x167 bytes. Retail body: Common.dll ?FlChecksum@@YAIPBXI@Z. It is
// not a CRC: nothing is masked between byte additions, so s1/s2 are plain
// 32-bit accumulators until each chunk end. The result packs s2:s1, which is
// what MP character files carry.
COMMON_API unsigned int __cdecl FlChecksum(void const* Data, unsigned long Size);
