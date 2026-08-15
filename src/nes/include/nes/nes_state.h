
typedef u8 NES_Input;
enum
{
	NES_INPUT_A      = 1,
	NES_INPUT_B      = 2,
	NES_INPUT_SELECT = 4,
	NES_INPUT_START  = 8,
	NES_INPUT_UP     = 16,
	NES_INPUT_DOWN   = 32,
	NES_INPUT_LEFT   = 64,
	NES_INPUT_RIGHT  = 128,
};

typedef struct
{
	u8  A;
	u8  X;
	u8  Y;
	u8  S;
	u8  P;
	u16 PC;
}
NES_CPUState;

// NOTE(RJ) this actually has to match OAM layout
typedef struct
{
	u8 ypos;
	u8 index;
	u8 attrs;
	u8 xpos;
}
NES_PPUSprite;

typedef struct
{
	u16           dot;
	u16           scanline;
	u16           t;
	u16           v;
	u8            x;
	u8            w;
	u8            tile_id;
	u8            tile_hi;
	u8            tile_lo;
	u8            atr_b;
	u8            atr_l0;
	u8            atr_l1;
	u16           chr_r0;
	u16           chr_r1;
	u8            atr_r0;
	u8            atr_r1;
	u8            spr0_enable;
	u8            spr0_2cycle_delay;
	// TODO(RJ): give these better names!
	u8            PPUCTRL;
	u8            PPUMASK;
	u8            PPUSTATUS;
	u8            OAMADDR;
	u8            data_read_buf;
	u8            nsprs;
	NES_PPUSprite sprs[NES_PPU_MAX_SPRITES_PER_SCANLINE];
	union
	{
		NES_PPUSprite OAM[ 64];
		u8           _oam[256];
	};
	u16 oam_address;
	u8 oam_latch;
	u8 soam_index;
	union
	{
		NES_PPUSprite SOAM[ 8];
		u8            soam[32];
	};
	u8 _pram[32];
}
NES_PPUState;

typedef struct
{
	u8 counter;
	u8 period;
}
NES_APUDivider;

typedef struct
{
	NES_APUDivider divider;
	u8 reload_divider;
	u8 shift;
	u8 enable;
	u8 negate;
}
NES_APUSweep;

typedef struct
{
	NES_APUDivider divider;
	u8             counter;
	u8             reload_divider;
}
NES_APUEnvelope;

typedef struct
{
	u8               enable;
	u8               infinite_play;
	u8               length_counter;
	u8               volume;
	u8               use_constant_volume;
	NES_APUSweep     sweep;
	NES_APUEnvelope  env;
	u8               duty_mask;
	u8               phase;
	u16              timer;
	u16              timer_period;
}
NES_APU_Pulse;

typedef struct
{
	u8  enable;

	u8  length_counter;
	u8  length_counter_halt;

	u8  linear_counter;
	u8  linear_counter_reload;
	u8  linear_counter_reload_value;

	u8  wave_phase;
	u16 wave_period;
	u16 wave_timer;
}
NES_APU_Triangle;

typedef struct
{
	u8              irq_pending;
	u8              irq_inhibit;
	u8              reset_delay;
	u8              reset_mode; // Last mode written to $4017, pending while reset_delay is nonzero.
	u8              mode;
	u8              step_index;
	u16             cpu_cycle_counter;
	NES_APU_Pulse    pulse[2];
	NES_APU_Triangle triangle;
}
NES_APUState;

typedef struct { u8 inputs[2]; } NES_InputState;


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
