#ifndef NES_EMULATOR_H
#define NES_EMULATOR_H

#include "base.h"

enum
{
	NES_PPU_MAX_SPRITES_PER_SCANLINE  = 8,
	NES_MAX_CHR_ROM_SIZE              = MB(1)    ,
	NES_MAX_PRG_ROM_SIZE              = MB(1)    ,
	NES_MAX_CHR_RAM_SIZE              = KiB(16)  ,
	NES_MAX_PRG_RAM_SIZE              = KiB(16)  ,
	NES_VRAM_SIZE                     = 0x2000   ,
	NES_WRAM_SIZE                     = 0x2000   ,
	NES_CPU_ADDRESS_SPACE             = 0x10000  ,
	NES_PPU_ADDRESS_SPACE             = 0x4000   ,
	NES_CPU_HZ                        = 1789773  ,
	NES_VIDEO_WIDTH                   = 256      ,
	NES_VIDEO_HEIGHT                  = 240      ,
	NES_PPU_FRAME_CYCLES              = 262 * 341,
};

#include "nes_state.h"

enum
{
	NES_PPU_EVENT_NONE  = 0,
	NES_PPU_EVENT_NMI   = 1 << 0,
	NES_PPU_EVENT_FRAME = 1 << 1,
};

typedef struct NES_Emulator NES_Emulator;

typedef enum
{
	NES_DEVICE_NONE = 0,
	NES_DEVICE_CPU,
	NES_DEVICE_PPU,
	NES_DEVICE_OAM,
	NES_DEVICE_PRAM,
	NES_DEVICE_VRAM,
	NES_DEVICE_WRAM,
	NES_DEVICE_CHR_ROM,
	NES_DEVICE_CHR_RAM,
	NES_DEVICE_PRG_ROM,
	NES_DEVICE_PRG_RAM,
	NES_DEVICE_COUNT,
	NES_DEVICE_ENUM_FORCE_U32 = MAX_VALUE_U32,
}
NES_DeviceId;

typedef struct
{
	NES_DeviceId device;
	// TODO(RJ) REMOVE ALIASES
	union
	{
		u32 offset;
		u32 address;
	};
}
NES_MapAddr;

static inline NES_MapAddr nes_map_addr(NES_DeviceId device, u32 address)
{
	return (NES_MapAddr) { device, address };
}

typedef enum
{
	NES_BUS_ACCESS_READ,
	NES_BUS_ACCESS_WRITE,
	NES_BUS_ACCESS_PEEK,
	// TODO(RJ) remove this additional mode??
	NES_BUS_ACCESS_MAP,
}
NES_BusAccessKind;

// TODO(RJ) why are we passing in so much stuff in here, we only need
// mode addr and data
// TODO(RJ) the result is the thing that contains the final addr the device and the data read.
typedef struct
{
	NES_BusAccessKind kind;
	u32               address;
	// TODO(RJ) literally remove this!
	NES_MapAddr       mapped;
	u8                value;
}
NES_BusAccess;

typedef NES_BusAccess (*NES_BusFunc)(NES_Emulator *nes, NES_BusAccess access);
#define NES_MAPPER_VALID_FUNC(NAME) b32 (NAME)(const NES_Emulator *nes)
#define NES_MAPPER_RSET_FUNC(NAME) b32 (NAME)(NES_Emulator *nes)
typedef NES_MAPPER_VALID_FUNC(*NES_MapperValidFunc);
typedef NES_MAPPER_RSET_FUNC(*NES_RstFunc);

typedef struct
{
	const char          *name;
	NES_MapperValidFunc  valid;
	NES_RstFunc          reset;
	NES_BusFunc          cpu_bus;
	NES_BusFunc          ppu_bus;
}
NES_MapperClass;



#include "nes_trace.h"

typedef struct
{
	u32       mapper;
	// TODO(RJ) pretty sure these two are just nametable arrangement, which should
	// be, vertical, horizontal, four_screen, or mapper_based
	b32      vmirror;
	b32  four_screen;
	b32  has_trainer;
	ByteSpan prg_rom;
	ByteSpan chr_rom;
}
NES_SetupParams;

#define NES_STATE_FIELDS                                                       \
	u64                   scheduler_clock;                                      \
	u64                   sample_phase;                                         \
	u32                   cpu_stall_cycles;                                     \
	NES_CPUState          cpu;                                                  \
	NES_PPUState          ppu;                                                  \
	NES_APUState          apu;                                                  \
	NES_InputState        input_state;                                          \
	u8                    controllers[2];                                       \
	u8                    values[32];                                           \
	u8                    _wram[NES_WRAM_SIZE];                                 \
	u8                    _vram[NES_VRAM_SIZE];                                 \
	u8                    chr_ram[NES_MAX_CHR_RAM_SIZE];                        \
	u8                    prg_ram[NES_MAX_PRG_RAM_SIZE];                        \
	u8                    video[NES_VIDEO_HEIGHT][NES_VIDEO_WIDTH]

typedef struct NES_State NES_State;
struct NES_State
{
	NES_STATE_FIELDS;
};

// TODO(RJ) not sure whether keeping all the memory in one block will pay off
typedef struct NES_Emulator NES_Emulator;
struct NES_Emulator
{
	u32                     mapper_number;
	u32                     prg_rom_size,  chr_rom_size;
	b32                     vmirror;
	NES_MapperClass         mapper;

	u8                      chr_rom[NES_MAX_CHR_ROM_SIZE];
	u8                      prg_rom[NES_MAX_PRG_ROM_SIZE];
	union
	{
		NES_State state;
		struct
		{
			NES_STATE_FIELDS;
		};
	};
};


b32 nes_emulator_valid(const NES_Emulator *emulator);
b32 nes_supports_setup_params(NES_SetupParams params);
b32 nes_setup_emulator(NES_Emulator *emulator, NES_SetupParams data);
// Reset live devices while preserving cartridge and RAM storage.
void nes_reset_emulator(NES_Emulator *emulator);

b32 nes_emulator_ready_to_run(const NES_Emulator *core);
u64 nes_emulator_scheduler_clock(const NES_Emulator *core);
u32 nes_emulator_step(NES_Emulator *core, NES_TraceEntry *trace);

typedef struct
{
	f32 *samples;
	u64 sample_capacity;
	NES_TraceEntry *trace;
	u64 trace_capacity;
}
NES_RunParams;

typedef struct
{
	u64 samples;
	u64 steps;
}
NES_RunFrameResult;

NES_RunFrameResult nes_emulator_run_frame(NES_Emulator *emulator, NES_RunParams params);
void nes_emulator_set_input(NES_Emulator *core, u32 player, NES_Input input);



static inline u64 nes_sample_rate(const NES_Emulator *emulator) {
	(void)emulator;
	return 48 * 1000;
}

static inline u64 nes_required_sample_capacity(void) {
	// 48000 / 60.1 = ~ 799
	// This is just an extra safe size, so we don't even have to check because
	// it's impossible ...
	return 1024 * 2;
}



static inline NES_BusAccess nes_bus_access_mapped(NES_BusAccess access, NES_DeviceId device)
{
	access.mapped.device = device;
	access.mapped.offset = access.address;
	return access;
}

typedef struct
{
	NES_MapAddr mapped;
	u8 value;
}
NES_MappedRead;

// TODO(RJ) why are there so many exposed variants!
NES_MappedRead nes_cpu_bus_read_mapped(NES_Emulator *core, u16 address);
static inline u8 nes_cpu_bus_read(NES_Emulator *core, u16 address) {
	return nes_cpu_bus_read_mapped(core, address).value;
}

u8 nes_emulator_cpu_peek(NES_Emulator *core, u16 address);
u16 nes_emulator_cpu_peek_word(NES_Emulator *core, u16 address);
NES_MapAddr nes_emulator_cpu_map(NES_Emulator *core, u16 address);

void nes_cpu_bus_write(NES_Emulator *core, u16 address, u8 value);
u8 nes_cpu_bus_peek(NES_Emulator *core, u16 address);
NES_BusAccess nes_cpu_bus_peek_mapped(NES_Emulator *core, u16 address);
NES_MapAddr nes_cpu_bus_map(NES_Emulator *core, u16 address);

u8 nes_ppu_bus_read(NES_Emulator *core, u16 address);
void nes_ppu_bus_write(NES_Emulator *core, u16 address, u8 value);
u8 nes_ppu_bus_peek(NES_Emulator *core, u16 address);
NES_MapAddr nes_ppu_bus_map(NES_Emulator *core, u16 address);

#endif
