#ifndef ORB_NES_SERIALIZE_H
#define ORB_NES_SERIALIZE_H

#include "base.h"
#include "nes/emulator.h"

ByteSpan orb_nes_state_encode(Arena *arena, const NES_Emulator *emulator);
b32 orb_nes_state_decode(NES_Emulator *emulator, ByteSpan state);

#endif
