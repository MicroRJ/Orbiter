#ifndef INES_IMPORTER_H
#define INES_IMPORTER_H

#include "nes/game.h"

b32 ines_import(ByteSpan source, NES_Game *game);

#endif
