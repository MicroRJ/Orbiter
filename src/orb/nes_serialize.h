#ifndef ORB_NES_SERIALIZE_H
#define ORB_NES_SERIALIZE_H

#include "nes/emulator.h"

b32 orb_transfer_save_state(ByteStream *stream, NES_State *state);

#endif
