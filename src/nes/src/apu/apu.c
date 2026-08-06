// https://www.nesdev.org/wiki/APU

#include "apu.h"
#include "../emulator_internal.h"

// Length-counter values selected by bits 3-7 of $4003/$4007.
static const u8 apu_length_table[32] =
{
	0x0A, 0xFE, 0x14, 0x02, 0x28, 0x04, 0x50, 0x06,
	0xA0, 0x08, 0x3C, 0x0A, 0x0E, 0x0C, 0x1A, 0x0E,
	0x0C, 0x10, 0x18, 0x12, 0x30, 0x14, 0x60, 0x16,
	0xC0, 0x18, 0x48, 0x1A, 0x10, 0x1C, 0x20, 0x1E,
};

enum
{
	// On an NTSC front-loader, the frame counter has already advanced this far
	// from its implicit $4017 write when reset-vector code begins.
	APU_RESET_VECTOR_DELAY_CPU_CYCLES = 10,
};

static void apu_clock_pulse_sweeps(NES_APUState *apu);
static void apu_clock_pulse_timer(NES_APU_Pulse *pulse);
static void apu_clock_envelope(NES_APUEnvelope *envelope, b32 infinite_play);
static inline void apu_half_frame_clock(NES_APUState *apu);
static inline void apu_quarter_frame_clock(NES_APUState *apu);

static void apu_restart_frame_counter(NES_APUState *apu, u8 value)
{
	apu->irq_pending = 0;
	apu->irq_inhibit = value >> 6 & 1;
	apu->reset_delay = 0;
	apu->reset_mode = value >> 7 & 1;
	apu->mode = apu->reset_mode;
	apu->step_index = 0;
	apu->cpu_cycle_counter = APU_RESET_VECTOR_DELAY_CPU_CYCLES;
	if (apu->mode)
	{
		apu_half_frame_clock(apu);
		apu_quarter_frame_clock(apu);
	}
}

void nes_apu_power_on(NES_APUState *apu)
{
	memory_zero(apu, sizeof(*apu));
	apu_restart_frame_counter(apu, 0x00);
}

void nes_apu_reset(NES_APUState *apu)
{
	// Reset behaves like an immediate $4015 = 0 followed by the last value
	// written to $4017. Channel register and waveform state otherwise survive.
	apu->pulse[0].enable = 0;
	apu->pulse[0].length_counter = 0;
	apu->pulse[1].enable = 0;
	apu->pulse[1].length_counter = 0;
	apu->triangle.enable = 0;
	apu->triangle.length_counter = 0;
	apu->triangle.wave_phase = 0;
	u8 frame_counter = apu->reset_mode << 7 | apu->irq_inhibit << 6;
	apu_restart_frame_counter(apu, frame_counter);
}

static NES_APU_Pulse *apu_pulse_from_register(NES_APUState *apu, u16 address)
{
	return &apu->pulse[address >> 2 & 1];
}

static inline void apu_half_frame_clock(NES_APUState *apu)
{
	apu_clock_pulse_sweeps(apu);

	NES_APU_Pulse *pulse1 = &apu->pulse[0];
	NES_APU_Pulse *pulse2 = &apu->pulse[1];
	NES_APU_Triangle *triangle = &apu->triangle;

	if (!pulse1->infinite_play && pulse1->length_counter > 0) {
		pulse1->length_counter = (u8)(pulse1->length_counter - 1);
	}
	if (!pulse2->infinite_play && pulse2->length_counter > 0) {
		pulse2->length_counter = (u8)(pulse2->length_counter - 1);
	}
	if (!triangle->length_counter_halt && triangle->length_counter > 0) {
		triangle->length_counter = (u8)(triangle->length_counter - 1);
	}
}

static inline void apu_quarter_frame_clock(NES_APUState *apu)
{
	apu_clock_envelope(&apu->pulse[0].env, apu->pulse[0].infinite_play);
	apu_clock_envelope(&apu->pulse[1].env, apu->pulse[1].infinite_play);
	NES_APU_Triangle *triangle = &apu->triangle;

	// """
	//	When the frame counter generates a linear counter clock, the following actions occur in order:
	//		- If the linear counter reload flag is set, the linear counter is reloaded with the counter reload
	//    value, otherwise if the linear counter is non-zero, it is decremented.
	//		- If the control flag is clear, the linear counter reload flag is cleared.
	// """

	if (triangle->linear_counter_reload) {
		triangle->linear_counter = (u8)(triangle->linear_counter_reload_value);
	}
	else if (triangle->linear_counter > 0) {
		triangle->linear_counter = (u8)(triangle->linear_counter - 1);
	}
	if (!triangle->length_counter_halt) {
		triangle->linear_counter_reload = (u8)(0);
	}
}

static void apu_write_frame_counter(NES_APUState *apu, u8 value)
{

	// https://www.nesdev.org/wiki/APU#Status_($4015)
	// """
	// Interrupt inhibit flag. If set, the frame interrupt flag is cleared,
	// otherwise it is unaffected.
	// """
	b32 irq_inhibit = value >> 6 & 1;
	apu->irq_inhibit = (u8)(irq_inhibit);
	if (irq_inhibit) apu->irq_pending = (u8)(0);

	b32 reset_mode = value >> 7 & 1;
	apu->reset_mode = (u8)(reset_mode);

	// """
	// If the write occurs during an APU cycle, the effects occur 3 CPU cycles
	// after the $4017 write cycle, and if the write occurs between APU cycles, the
	// effects occurs 4 CPU cycles after the write cycle.
	// """
	// By APU cycle am guessing it means every other CPU cycle ...
	// We'll do 4 for now.
	//
	apu->reset_delay = (u8)(4);
}

static void apu_access_status(NES_APUState *apu, NES_BusAccess *access)
{
	if (access->kind == NES_BUS_ACCESS_WRITE)
	{
		apu->pulse[0].enable = (u8)(access->value >> 0 & 1);
		apu->pulse[1].enable = (u8)(access->value >> 1 & 1);
		apu->triangle.enable = (u8)(access->value >> 2 & 1);

		// Writing a zero to a channel enable bit silences the channel and
		// clears its length counter.
		if (!apu->pulse[0].enable) {
			apu->pulse[0].length_counter = (u8)(0);
		}
		if (!apu->pulse[1].enable) {
			apu->pulse[1].length_counter = (u8)(0);
		}
		if (!apu->triangle.enable) {
			apu->triangle.length_counter = (u8)(0);
		}
	}
	else
	{
		// "will read as 1 if the corresponding length counter has not been halted through either
		// expiring or a write of 0 to the corresponding bit."
		access->value = 0;
		access->value |= (apu->pulse[0].length_counter != 0) << 0;
		access->value |= (apu->pulse[1].length_counter != 0) << 1;
		access->value |= (apu->triangle.length_counter != 0) << 2;
		access->value |= apu->irq_pending << 6;
		// """
		// can be cleared either by reading $4015 (which also returns its old status) or by setting the
		// interrupt inhibit flag.
		// """
		apu->irq_pending = (u8)(0);
	}
}

static void apu_pulse_write_control(NES_APU_Pulse *pulse, u8 value)
{
	static const u8 duty_masks[] = { 2, 6, 30, 249 };
	pulse->duty_mask = (u8)(duty_masks[value >> 6 & 3]);
	pulse->infinite_play = (u8)(value >> 5 & 1);
	pulse->use_constant_volume = (u8)(value >> 4 & 1);
	pulse->env.divider.period = (u8)(value & 15);
	pulse->volume = (u8)(value & 15);
}

static void apu_pulse_write_sweep(NES_APU_Pulse *pulse, u8 value)
{
	pulse->sweep.enable = (u8)(value >> 7 & 1);
	pulse->sweep.divider.period = (u8)(value >> 4 & 7);
	pulse->sweep.negate = (u8)(value >> 3 & 1);
	pulse->sweep.shift = (u8)(value & 7);
	pulse->sweep.reload_divider = (u8)(true);
}

static void apu_pulse_write_timer_low(NES_APU_Pulse *pulse, u8 value)
{
	pulse->timer_period = (u16)((pulse->timer_period & 0x0700) | value);
}

// "Writing to $4003/$4007 reloads the length counter, restarts the envelope, and resets the phase of the pulse generator."
// "The sequencer is immediately restarted at the first value of the current sequence."
// "The envelope is also restarted. The period divider is not reset."
// "Writing a zero to any of the channel enable bits (NT21) will silence that channel and halt its length counter."
static void apu_pulse_write_timer_high(NES_APU_Pulse *pulse, u8 value)
{
	pulse->timer_period = (u16)((pulse->timer_period & 0x00FF) | (value & 7) << 8);

	if (pulse->enable) {
		pulse->length_counter = (u8)(apu_length_table[value >> 3]);
	}
	pulse->phase = (u8)(0);
	pulse->env.reload_divider = (u8)(1);
}

NES_BusAccess nes_apu_register_access(NES_Emulator *core, NES_BusAccess access)
{
	NES_APUState *apu = &core->apu;
	switch (access.address)
	{
		case 0x4017:
		{
			if (access.kind == NES_BUS_ACCESS_WRITE) {
				apu_write_frame_counter(apu, access.value);
			}
		} break;

		case 0x4015:
		{
			apu_access_status(apu, &access);
		} break;

		// https://www.nesdev.org/wiki/APU_Pulse#Registers
		// "Note: the addresses below are write-only! Reading from these addresses exhibits open-bus behavior."
		case 0x4000: case 0x4004:
		{
			if (access.kind == NES_BUS_ACCESS_WRITE) {
				apu_pulse_write_control(apu_pulse_from_register(apu, access.address), access.value);
			}
		} break;

		case 0x4001: case 0x4005:
		{
			if (access.kind == NES_BUS_ACCESS_WRITE) {
				apu_pulse_write_sweep(apu_pulse_from_register(apu, access.address), access.value);
			}
		} break;

		case 0x4002: case 0x4006:
		{
			if (access.kind == NES_BUS_ACCESS_WRITE) {
				apu_pulse_write_timer_low(apu_pulse_from_register(apu, access.address), access.value);
			}
		} break;

		case 0x4003: case 0x4007:
		{
			if (access.kind == NES_BUS_ACCESS_WRITE) {
				apu_pulse_write_timer_high(apu_pulse_from_register(apu, access.address), access.value);
			}
		} break;
		case 0x4008: {
			if (access.kind == NES_BUS_ACCESS_WRITE) {
				NES_APU_Triangle *triangle = & apu->triangle;
				triangle->length_counter_halt = (u8)(access.value >> 7);
				triangle->linear_counter_reload_value = (u8)(access.value & 0x7F);
			}
		}
		break;
		case 0x400A: {
			if (access.kind == NES_BUS_ACCESS_WRITE) {
				NES_APU_Triangle *triangle = & apu->triangle;
				triangle->wave_period = (u16)(triangle->wave_period & 0x0700 | access.value);
			}
		}
		break;
		case 0x400B: {
			if (access.kind == NES_BUS_ACCESS_WRITE) {
				NES_APU_Triangle *triangle = & apu->triangle;
				triangle->wave_period = (u16)(triangle->wave_period & 0x00FF | (access.value & 7) << 8);
				if (triangle->enable) {
					triangle->length_counter = (u8)(apu_length_table[access.value >> 3]);
				}
				triangle->linear_counter_reload = (u8)(1);
			}
		}
		break;
	}

	return access;
}

// https://www.nesdev.org/wiki/APU_Envelope
static void apu_clock_envelope(NES_APUEnvelope *envelope, b32 infinite_play)
{
	if (envelope->reload_divider)
	{
		envelope->reload_divider = (u8)(false);
		envelope->divider.counter = (u8)(envelope->divider.period);
		envelope->counter = (u8)(15);
	}
	else if (envelope->divider.counter > 0)
	{
		envelope->divider.counter = (u8)(envelope->divider.counter - 1);
	}
	else
	{
		envelope->divider.counter = (u8)(envelope->divider.period);
		if (envelope->counter > 0)
		{
			envelope->counter = (u8)(envelope->counter - 1);
		}
		else if (infinite_play)
		{
			envelope->counter = (u8)(15);
		}
	}
}


// "A period of t < 8, either set explicitly or via a sweep period update, silences the corresponding
// pulse channel. The highest frequency a pulse channel can output is hence about 12.4 kHz for NTSC."
static i32 apu_pulse_sweep_target(const NES_APU_Pulse *pulse, u32 pulse_index)
{
	i32 change = pulse->timer_period >> pulse->sweep.shift;
	if (pulse->sweep.negate) {
		change = -change - (pulse_index == 0);
	}
	return Max((i32)pulse->timer_period + change, 0);
}

static b32 apu_pulse_is_muted(const NES_APU_Pulse *pulse, u32 pulse_index)
{
	return !pulse->enable || !pulse->length_counter || pulse->timer_period < 8 ||
		apu_pulse_sweep_target(pulse, pulse_index) >= APU_MAX_PULSE_TIMER_VALUE;
}

static void apu_clock_pulse_timer(NES_APU_Pulse *pulse)
{
	if (pulse->timer == 0)
	{
		pulse->timer = (u16)(pulse->timer_period);
		// The waveform is 8 steps. Hardware clocks this mechanism every other CPU
		// cycle, BUT WE step the EVERY SINGLE CPU cycle therefore phase increases
		// twice as fast, so we just let it get to 8 * 2 but when using it we divide
		// by 2.
		pulse->phase = (u8)((pulse->phase + 1) & 15);
	}
	else
	{
		pulse->timer = (u16)(pulse->timer - 1);
	}
}

static void apu_clock_pulse_sweeps(NES_APUState *apu)
{
	for (u32 pulse_index = 0; pulse_index < ArrayCount(apu->pulse); ++pulse_index)
	{
		NES_APU_Pulse *pulse = &apu->pulse[pulse_index];
		NES_APUSweep *sweep = &pulse->sweep;

		if (!sweep->divider.counter && sweep->enable && sweep->shift &&
			!apu_pulse_is_muted(pulse, pulse_index))
		{
			pulse->timer_period = (u16)(apu_pulse_sweep_target(pulse, pulse_index));
		}

		if (!sweep->divider.counter || sweep->reload_divider)
		{
			sweep->divider.counter = (u8)(sweep->divider.period);
			sweep->reload_divider = (u8)(false);
		}
		else
		{
			sweep->divider.counter = (u8)(sweep->divider.counter - 1);
		}
	}
}

typedef struct
{
	u32 starts_at_cpu_cycles;
	b32 quarter_frame;
	b32 half_frame;
	b32 frame_interrupt;
}
StepDesc;

static const StepDesc step_desc4[] = {
	{3728  * 2 + 1, 1, 0, 0 },
	{7456  * 2 + 1, 1, 1, 0 },
	{11185 * 2 + 1, 1, 0, 0 },
	{14914 * 2 + 1, 1, 1, 1 },
};

static const StepDesc step_desc5[] = {
	{3728  * 2 + 1, 1, 0, 0 },
	{7456  * 2 + 1, 1, 1, 0 },
	{11185 * 2 + 1, 1, 0, 0 },
	{14914 * 2 + 1, 0, 0, 0 },
	{18640 * 2 + 1, 1, 1, 0 },
};

// https://www.nesdev.org/wiki/APU_Frame_Counter
// Quarter- and half-frame outputs occur one CPU cycle after the corresponding
// every-other-cycle sequencer count, hence the +1 values above. The CPU still
// executes instructions atomically, so a $4017 write cannot yet be placed on
// its exact read/write phase. Until CPU accesses are cycle-scheduled, reset
// delay uses the deterministic four-cycle side of the hardware's 3/4-cycle
// behavior and the final four-step IRQ window is represented by one assertion.
static void apu_clock_frame_sequencer(NES_APUState *apu)
{

	// """
	// After 3 or 4 CPU clock cycles*, the timer is reset.
	// """
	if (apu->reset_delay > 0)
	{
		apu->reset_delay = (u8)(apu->reset_delay - 1);
		if (apu->reset_delay == 0)
		{
			apu->step_index = (u8)(0);
			apu->cpu_cycle_counter = (u16)(0);
			// """
			// If the mode flag is set, then both "quarter frame" and "half frame" signals
			// are also generated.
			// """
			if (apu->reset_mode == 1) {
				apu_half_frame_clock(apu);
				apu_quarter_frame_clock(apu);
			}
			apu->mode = (u8)(apu->reset_mode);

			return;
		}
	}

	const StepDesc *step_desc = apu->mode ? step_desc5 : step_desc4;
	b32 frame_interrupt = step_desc[apu->step_index].frame_interrupt;
	b32 clock_quarter_frame = step_desc[apu->step_index].quarter_frame;
	b32 clock_half_frame = step_desc[apu->step_index].half_frame;
	u32 starts_at_cpu_cycles = step_desc[apu->step_index].starts_at_cpu_cycles;
	u32 frame_size = apu->mode ? 5 : 4;
	apu->cpu_cycle_counter = (u16)(apu->cpu_cycle_counter + 1);
	if (apu->cpu_cycle_counter < starts_at_cpu_cycles) return;

	// The scheduler derives the CPU IRQ line from this and the other IRQ sources.
	if (apu->mode == 0 && frame_interrupt && apu->irq_inhibit == 0) {
		apu->irq_pending = (u8)(1);
	}
	if (clock_half_frame) apu_half_frame_clock(apu);
	if (clock_quarter_frame) apu_quarter_frame_clock(apu);
	apu->step_index = (u8)(apu->step_index + 1);
	if (apu->step_index >= frame_size) {
		apu->step_index = (u8)(0);
		apu->cpu_cycle_counter = (u16)(0);
	}
}

void nes_apu_clock_cpu_cycle(NES_APUState *apu)
{
	apu_clock_pulse_timer(&apu->pulse[0]);
	apu_clock_pulse_timer(&apu->pulse[1]);

	NES_APU_Triangle *triangle = &apu->triangle;
	if (triangle->wave_timer == 0) {
		triangle->wave_timer = (u16)(triangle->wave_period);
		if (triangle->linear_counter != 0 && triangle->length_counter != 0) {
			triangle->wave_phase = (u8)((triangle->wave_phase + 1) & 31);
		}
	}
	else {
		triangle->wave_timer = (u16)(triangle->wave_timer - 1);
	}



	apu_clock_frame_sequencer(apu);
}
// NES APU implementation.


static const u8 triangle_wavetable[] = {
	15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
 	0 ,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
};

// """
// The envelope unit's volume output depends on the constant volume flag: if set,
// the envelope parameter directly sets the volume, otherwise the decay level is
// the current volume. The constant volume flag has no effect besides selecting the
// volume source; the decay level will still be updated when constant volume is selected.
// """
// """
// The mixer receives the pulse channel's current envelope volume (lower 4 bits from $4000 or $4004) except when
// The sequencer output is zero, or overflow from the sweep unit's adder is silencing the channel, or
// the length counter is zero, or the timer has a value less than eight (t<8, noted above).
// If any of the above are true, then the pulse channel sends zero (silence) to the mixer.
// """
static inline u32 apu_pulse_output(const NES_APU_Pulse *pulse, u32 pulse_index)
{
	u32 volume = pulse->env.counter;
	if (apu_pulse_is_muted(pulse, pulse_index)) {
		volume = 0;
	} else if (pulse->use_constant_volume) {
		volume = pulse->volume;
	}
	u32 output = pulse->duty_mask >> (pulse->phase >> 1) & 1;
	return output * volume;
}

f32 nes_apu_dac(const NES_APUState *apu)
{
	f32 pulse_out = apu_pulse_output(&apu->pulse[0], 0) + apu_pulse_output(&apu->pulse[1], 1);
	if (pulse_out != 0) pulse_out = 95.88f / (8128.f / pulse_out + 100.f);

	f32 tnd_out = triangle_wavetable[apu->triangle.wave_phase] / 8227.f;
	if (tnd_out != 0) tnd_out = 159.f / (1.f / tnd_out + 100.f);

	f32 output = pulse_out + tnd_out;
	return output;
}
