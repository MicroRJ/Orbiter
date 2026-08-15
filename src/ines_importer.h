#ifndef INES_IMPORTER_H
#define INES_IMPORTER_H

#include "nes/emulator.h"

// TODO(RJ): this should instead take a byte span and produce pointers, should not take an arena!
NES_Game *ines_import_file(Arena *arena, Str path, Str *title);

#endif
