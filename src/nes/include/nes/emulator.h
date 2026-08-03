#ifndef NES_EMULATOR_H
#define NES_EMULATOR_H

#include "base.h"
#include "nes/cartridge.h"

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

	NES_SCHEDULER_TRACE_CAPACITY_POW2 = 1 << 16,
	NES_SCHEDULER_TRACE_CAPACITY_MASK = NES_SCHEDULER_TRACE_CAPACITY_POW2 - 1,
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

typedef struct
{
	u64 reads;
	u64 writes;
}
NES_BusMetrics;


typedef NES_BusAccess (*NES_BusFunc)(NES_Emulator *nes, NES_BusAccess access);
#define NES_MAPPER_RSET_FUNC(NAME) b32 (NAME)(NES_Emulator *nes)
typedef NES_MAPPER_RSET_FUNC(*NES_RstFunc);

typedef struct
{
	const char     *name;
	NES_RstFunc    reset;
	NES_BusFunc  cpu_bus;
	NES_BusFunc  ppu_bus;
}
NES_MapperClass;



#include "nes_trace.h"

struct NES_Emulator
{
	// TODO(RJ) not sure that keeping all the memory in one block is paying off
	u32                     mapper_number;
	u32                     num_chr_banks, num_prg_banks;
	u32                     prg_rom_size,  chr_rom_size;
	u32                     prg_bank_size, chr_bank_size;
	b32                     vmirror;
	u8                      values[32];
	NES_InputState          input_state;
	u32                     cpu_stall_cycles;
	NES_CPUState            cpu;
	NES_PPUState            ppu;
	NES_APUState            apu;
	u8                      controllers[2];
	u8                      _wram[NES_WRAM_SIZE];
	u8                      _vram[NES_VRAM_SIZE];
	u8                      chr_ram[NES_MAX_CHR_RAM_SIZE];
	u8                      prg_ram[NES_MAX_PRG_RAM_SIZE];
	u8                      chr_rom[NES_MAX_CHR_ROM_SIZE];
	u8                      prg_rom[NES_MAX_PRG_ROM_SIZE];
	NES_MapperClass         mapper;
	// TODO(RJ) a better name would be emulation step
	u64                     scheduler_clock;
	NES_BusMetrics          cpu_bus_metrics;
	NES_BusMetrics          ppu_bus_metrics;
	u64                     scheduler_trace_index;
	u64                     sample_phase;
	u8                      video[NES_VIDEO_HEIGHT][NES_VIDEO_WIDTH];
	// TODO(RJ) do we store this here or do we feed it in on run?
	NES_PackedTraceEntry scheduler_trace[NES_SCHEDULER_TRACE_CAPACITY_POW2];
};
NES_SchedulerTraceView nes_emulator_scheduler_trace(const NES_Emulator *core);

u32 nes_emulator_prg_rom_size(const NES_Emulator *core);

b32 nes_emulator_has_cartridge(const NES_Emulator *core);
b32 nes_emulator_supports_cartridge(NES_CartridgeInfo cartridge);
u64 nes_emulator_scheduler_clock(const NES_Emulator *core);

u32 nes_emulator_step(NES_Emulator *core);

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

typedef struct
{
	u64 samples;
	u64 steps;
}
NES_RunFrameResult;

NES_RunFrameResult nes_emulator_run_frame(NES_Emulator *emulator, f32 *sample_buffer, u64 sample_capacity);

void nes_emulator_set_input(NES_Emulator *core, u32 player, NES_Input input);
u8 nes_emulator_cpu_peek(NES_Emulator *core, u16 address);
u16 nes_emulator_cpu_peek_word(NES_Emulator *core, u16 address);
NES_MapAddr nes_emulator_cpu_map(NES_Emulator *core, u16 address);

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

NES_BusAccess nes_oam_mem_access(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess nes_pram_access(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess nes_vram_access(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess nes_wram_access(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess nes_chr_rom_access(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess nes_prg_rom_access(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess nes_chr_ram_access(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess nes_prg_ram_access(NES_Emulator *nes, NES_BusAccess access);

// The CPU address space exposes operations with explicit semantics. The
// generic bus callback remains an implementation detail of devices and
// mappers; CPU execution and debugger code should not construct bus modes.
NES_MappedRead nes_cpu_bus_read_mapped(NES_Emulator *core, u16 address);
static inline u8 nes_cpu_bus_read(NES_Emulator *core, u16 address)
{
	return nes_cpu_bus_read_mapped(core, address).value;
}
void nes_cpu_bus_write(NES_Emulator *core, u16 address, u8 value);
u8   nes_cpu_bus_peek(NES_Emulator *core, u16 address);

NES_BusAccess nes_cpu_bus_peek_mapped(NES_Emulator *core, u16 address);
NES_MapAddr nes_cpu_bus_map(NES_Emulator *core, u16 address);

u8          nes_ppu_bus_read(NES_Emulator *core, u16 address);
void        nes_ppu_bus_write(NES_Emulator *core, u16 address, u8 value);
u8          nes_ppu_bus_peek(NES_Emulator *core, u16 address);
NES_MapAddr nes_ppu_bus_map(NES_Emulator *core, u16 address);

#if 0
ByteSpan nes_emulator_save_state(NES_Emulator *core, Arena *arena);
b32 nes_emulator_load_state(NES_Emulator *core, ByteSpan state);
b32 nes_emulator_load_cartridge(NES_Emulator *core, NES_CartridgeDesc cartridge);
#endif


#endif
