#ifndef NES_SERIALIZE_H
#define NES_SERIALIZE_H

#include "nes/emulator.h"

b32 nes_serialize_state(ByteStream *stream, NES_State *state);

#endif
