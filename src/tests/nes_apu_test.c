#include "base.h"
#include "apu/apu.h"
#include "emulator_internal.h"
#include "nes/state_meta.h"
#include <stdio.h>

static u32 apu_test_failures;
static const char *apu_test_name;
static NES_Emulator *apu_test_core;

static void apu_expect_equal_(u64 expected, u64 actual,
	const char *expression, i32 line)
{
	if (expected != actual)
	{
		fprintf(stderr, "%s:%d: %s: expected 0x%llX, got 0x%llX\n",
			__FILE__, line, apu_test_name, expected, actual);
		fprintf(stderr, "    %s\n", expression);
		++apu_test_failures;
	}
}

#define APU_EXPECT_EQUAL(expected, actual) \
	apu_expect_equal_((u64)(expected), (u64)(actual), #actual, __LINE__)

static NES_BusAccess apu_access(NES_BusAccessKind kind, u16 address, u8 value)
{
	return nes_apu_register_access(apu_test_core, (NES_BusAccess) {
		.kind = kind,
		.address = address,
		.value = value,
	});
}

static void apu_write(u16 address, u8 value)
{
	apu_access(NES_BUS_ACCESS_WRITE, address, value);
}

static u8 apu_read(u16 address)
{
	return apu_access(NES_BUS_ACCESS_READ, address, 0).value;
}


static void apu_test_triangle_registers(void)
{
	apu_test_name = "triangle register decoding";

	NES_APUState *apu = &apu_test_core->apu;
	nes_apu_reset(apu);

	NES_APU_Triangle *triangle = &apu->triangle;
	apu_write(0x4015, 0x04);
	APU_EXPECT_EQUAL(1, triangle->enable);

	apu_write(0x4008, 0x80);
	APU_EXPECT_EQUAL(1, triangle->length_counter_halt);
	APU_EXPECT_EQUAL(0, triangle->linear_counter_reload_value);

	apu_write(0x4008, 0x7F);
	APU_EXPECT_EQUAL(0x00, triangle->length_counter_halt);
	APU_EXPECT_EQUAL(0x7F, triangle->linear_counter_reload_value);

	// writing to timer low byte doesn't affect high byte
	apu_write(0x400A, 0xFF);
	APU_EXPECT_EQUAL(0xFF, triangle->wave_period);
	APU_EXPECT_EQUAL(0x00, triangle->wave_period >> 8);
	apu_write(0x400A, 0x00);
	APU_EXPECT_EQUAL(0x00, triangle->wave_period);

	apu_write(0x400B, 0x07);
	APU_EXPECT_EQUAL(0x0700, triangle->wave_period);
	apu_write(0x400B, 0xF8);
	APU_EXPECT_EQUAL(0x00, triangle->wave_period);
	// ensure loads the last value from the length table
	APU_EXPECT_EQUAL(0x1E, triangle->length_counter);
	APU_EXPECT_EQUAL(0x04, apu_read(0x4015));

}

static void apu_test_triangle_counters_and_timer(void)
{
	apu_test_name = "triangle counters and timer";
	NES_APUState *apu = &apu_test_core->apu;
	NES_APU_Triangle *triangle = &apu->triangle;
	nes_apu_reset(apu);

	triangle->linear_counter_reload = 1;
	triangle->linear_counter_reload_value = 5;
	triangle->length_counter_halt = 0;
	apu->step_index = 0;
	apu->cpu_cycle_counter = 3728 * 2;
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(5, triangle->linear_counter);
	APU_EXPECT_EQUAL(0, triangle->linear_counter_reload);

	apu->step_index = 0;
	apu->cpu_cycle_counter = 3728 * 2;
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(4, triangle->linear_counter);

	triangle->linear_counter_reload = 1;
	triangle->linear_counter_reload_value = 7;
	triangle->length_counter_halt = 1;
	apu->step_index = 0;
	apu->cpu_cycle_counter = 3728 * 2;
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(7, triangle->linear_counter);
	APU_EXPECT_EQUAL(1, triangle->linear_counter_reload);

	triangle->wave_period = 2;
	triangle->wave_timer = 0;
	triangle->wave_phase = 31;
	triangle->linear_counter = 1;
	triangle->length_counter = 1;
	apu->step_index = 0;
	apu->cpu_cycle_counter = 0;
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(2, triangle->wave_timer);
	APU_EXPECT_EQUAL(0, triangle->wave_phase);
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(0, triangle->wave_timer);
	APU_EXPECT_EQUAL(0, triangle->wave_phase);
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(2, triangle->wave_timer);
	APU_EXPECT_EQUAL(1, triangle->wave_phase);

	triangle->wave_timer = 0;
	triangle->linear_counter = 0;
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(1, triangle->wave_phase);
}

static void apu_test_triangle_metadata(void)
{
	apu_test_name = "triangle state metadata";
	const NES_StateRecord *apu_record = nes_state_record(NES_RECORD_APU);
	const NES_StateRecord *triangle_record = nes_state_record(NES_RECORD_APU_TRIANGLE);
	APU_EXPECT_EQUAL(sizeof(NES_APUState), apu_record->size);
	APU_EXPECT_EQUAL(sizeof(NES_APU_Triangle), triangle_record->size);
	APU_EXPECT_EQUAL(9, triangle_record->field_count);
	const NES_StateField *field = nes_state_field_from_id(NES_STATE_FIELD_ID_APU_TRIANGLE);
	APU_EXPECT_EQUAL(offsetof(NES_APUState, triangle), field->offset);
	APU_EXPECT_EQUAL(NES_RECORD_APU_TRIANGLE, field->record_id);
}

static void apu_test_reset_and_registers(void)
{
	apu_test_name = "reset and pulse register decoding";
	NES_APUState *apu = &apu_test_core->apu;
	memset(apu, 0xCC, sizeof(*apu));
	nes_apu_reset(apu);

	APU_EXPECT_EQUAL(0, apu->mode);
	APU_EXPECT_EQUAL(0, apu->step_index);
	APU_EXPECT_EQUAL(0, apu->cpu_cycle_counter);
	APU_EXPECT_EQUAL(0, apu->pulse[0].enable);
	APU_EXPECT_EQUAL(0, apu->pulse[1].enable);

	apu_write(0x4000, 0xBB);
	NES_APU_Pulse *pulse = &apu->pulse[0];
	APU_EXPECT_EQUAL(30, pulse->duty_mask);
	APU_EXPECT_EQUAL(1, pulse->infinite_play);
	APU_EXPECT_EQUAL(1, pulse->use_constant_volume);
	APU_EXPECT_EQUAL(11, pulse->volume);
	APU_EXPECT_EQUAL(11, pulse->env.divider.period);
	APU_EXPECT_EQUAL(0, pulse->env.reload_divider);

	apu_write(0x4001, 0xDF);
	APU_EXPECT_EQUAL(1, pulse->sweep.enable);
	APU_EXPECT_EQUAL(5, pulse->sweep.divider.period);
	APU_EXPECT_EQUAL(1, pulse->sweep.negate);
	APU_EXPECT_EQUAL(7, pulse->sweep.shift);
	APU_EXPECT_EQUAL(1, pulse->sweep.reload_divider);
	apu_write(0x4004, 0x41);
	APU_EXPECT_EQUAL(6, apu->pulse[1].duty_mask);
	APU_EXPECT_EQUAL(1, apu->pulse[1].volume);
	APU_EXPECT_EQUAL(30, apu->pulse[0].duty_mask);

	apu_write(0x4002, 0xA5);
	pulse->phase = 7;
	pulse->env.counter = 9;
	pulse->env.reload_divider = false;
	apu_write(0x4003, 0x1D);
	APU_EXPECT_EQUAL(0x5A5, pulse->timer_period);
	APU_EXPECT_EQUAL(0, pulse->phase);
	APU_EXPECT_EQUAL(9, pulse->env.counter);
	APU_EXPECT_EQUAL(1, pulse->env.reload_divider);
	APU_EXPECT_EQUAL(0, pulse->length_counter);

	pulse->volume = 9;
	apu_read(0x4000);
	APU_EXPECT_EQUAL(9, pulse->volume);

	apu->mode = 1;
	apu->step_index = 3;
	apu->cpu_cycle_counter = 4;
	apu->irq_pending = 1;
	apu_write(0x4017, 0x40);
	APU_EXPECT_EQUAL(1, apu->mode);
	APU_EXPECT_EQUAL(3, apu->step_index);
	APU_EXPECT_EQUAL(4, apu->cpu_cycle_counter);
	APU_EXPECT_EQUAL(0, apu->irq_pending);
	APU_EXPECT_EQUAL(1, apu->irq_inhibit);
	APU_EXPECT_EQUAL(0, apu->reset_mode);
	APU_EXPECT_EQUAL(4, apu->reset_delay);
	for (u32 cycle = 0; cycle < 4; ++cycle) nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(0, apu->mode);
	APU_EXPECT_EQUAL(0, apu->step_index);
	APU_EXPECT_EQUAL(0, apu->cpu_cycle_counter);
	APU_EXPECT_EQUAL(0, apu->reset_delay);
}

static void apu_test_status_register(void)
{
	apu_test_name = "$4015 status register";
	NES_APUState *apu = &apu_test_core->apu;
	nes_apu_reset(apu);
	apu->pulse[0].length_counter = 4;
	apu->pulse[1].length_counter = 5;
	apu->triangle.length_counter = 6;
	apu_write(0x4015, 0x07);
	APU_EXPECT_EQUAL(1, apu->pulse[0].enable);
	APU_EXPECT_EQUAL(1, apu->pulse[1].enable);
	APU_EXPECT_EQUAL(1, apu->triangle.enable);
	APU_EXPECT_EQUAL(4, apu->pulse[0].length_counter);
	APU_EXPECT_EQUAL(5, apu->pulse[1].length_counter);
	APU_EXPECT_EQUAL(6, apu->triangle.length_counter);

	apu_write(0x4015, 0x01);
	APU_EXPECT_EQUAL(1, apu->pulse[0].enable);
	APU_EXPECT_EQUAL(0, apu->pulse[1].enable);
	APU_EXPECT_EQUAL(0, apu->pulse[1].length_counter);

	apu->pulse[0].length_counter = 0;
	APU_EXPECT_EQUAL(0, apu_read(0x4015) & 1);
}

static void apu_test_frame_clock(void)
{
	apu_test_name = "frame sequencer clocks";
	NES_APUState *apu = &apu_test_core->apu;
	nes_apu_reset(apu);
	for (u32 cycle = 0; cycle < 3728 * 2 - 1; ++cycle) {
		nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	}
	APU_EXPECT_EQUAL(3728 * 2 - 1, apu->cpu_cycle_counter);
	APU_EXPECT_EQUAL(0, apu->step_index);

	apu->pulse[0].env.reload_divider = true;
	apu->pulse[0].env.divider.period = 3;
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(3728 * 2, apu->cpu_cycle_counter);
	APU_EXPECT_EQUAL(0, apu->step_index);
	APU_EXPECT_EQUAL(1, apu->pulse[0].env.reload_divider);
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(3728 * 2 + 1, apu->cpu_cycle_counter);
	APU_EXPECT_EQUAL(1, apu->step_index);
	APU_EXPECT_EQUAL(0, apu->pulse[0].env.reload_divider);
	APU_EXPECT_EQUAL(3, apu->pulse[0].env.divider.counter);
	APU_EXPECT_EQUAL(15, apu->pulse[0].env.counter);

	apu->step_index = 1;
	apu->cpu_cycle_counter = 7456 * 2;
	apu->pulse[0].length_counter = 3;
	apu->pulse[0].infinite_play = false;
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(2, apu->pulse[0].length_counter);

	apu->step_index = 3;
	apu->cpu_cycle_counter = 14914 * 2;
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(0, apu->step_index);
	APU_EXPECT_EQUAL(0, apu->cpu_cycle_counter);

	nes_apu_reset(apu);
	apu->pulse[0].enable = true;
	apu->pulse[0].length_counter = 2;
	apu_write(0x4017, 0x80);
	for (u32 cycle = 0; cycle < 4; ++cycle) nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(1, apu->pulse[0].length_counter);
	APU_EXPECT_EQUAL(1, apu->mode);
	APU_EXPECT_EQUAL(0, apu->step_index);
	APU_EXPECT_EQUAL(0, apu->cpu_cycle_counter);
}

static void apu_make_audible_pulse(NES_APUState *apu)
{
	NES_APU_Pulse *pulse = &apu->pulse[0];
	pulse->enable = true;
	pulse->infinite_play = true;
	pulse->length_counter = 1;
	pulse->volume = 15;
	pulse->use_constant_volume = true;
	pulse->duty_mask = 0xFF;
	pulse->timer_period = 100;
	pulse->timer = 100;
}

static void apu_test_pulse_output_and_sweep(void)
{
#if 0
	apu_test_name = "pulse output and sweep";
	NES_APUState *apu = &apu_test_core->apu;
	nes_apu_reset(apu);
	APU_EXPECT_EQUAL(0, nes_apu_dac(apu) != 0.0f);

	apu_make_audible_pulse(apu);
	APU_EXPECT_EQUAL(1, nes_apu_dac(apu) > 0.0f);
	APU_EXPECT_EQUAL(1, nes_apu_dac(apu) > 0.1f);

	apu->pulse[0].duty_mask = 0;
	APU_EXPECT_EQUAL(1, nes_apu_dac(apu) == 0.0f);

	apu_make_audible_pulse(apu);
	apu->pulse[0].timer = 7;
	APU_EXPECT_EQUAL(1, nes_apu_dac(apu) != 0.0f);

	apu_make_audible_pulse(apu);
	apu->pulse[0].use_constant_volume = false;
	apu->pulse[0].env.counter = 1;
	f32 quiet_envelope = nes_apu_dac(apu);
	apu->pulse[0].env.counter = 15;
	f32 loud_envelope = nes_apu_dac(apu);
	APU_EXPECT_EQUAL(1, quiet_envelope != loud_envelope);

	apu_make_audible_pulse(apu);
	apu->pulse[0].timer = 100;
	apu->pulse[0].timer_period = 200;
	apu->pulse[0].sweep.enable = true;
	apu->pulse[0].sweep.shift = 1;
	apu->pulse[0].sweep.divider.counter = 0;
	apu->step_index = 1;
	apu->cpu_cycle_counter = 7456 * 2;
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(300, apu->pulse[0].timer_period);

	apu_make_audible_pulse(apu);
	apu->pulse[0].timer_period = 0x400;
	apu->pulse[0].sweep.enable = false;
	apu->pulse[0].sweep.negate = false;
	apu->pulse[0].sweep.shift = 0;
	APU_EXPECT_EQUAL(1, nes_apu_dac(apu) == 0.0f);

	apu_make_audible_pulse(apu);
	apu->pulse[0].timer = 8;
	apu->pulse[0].phase = 0;
	for (u32 cycle = 0; cycle < 8; ++cycle) nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(0, apu->pulse[0].phase);
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(1, apu->pulse[0].phase);

	apu_make_audible_pulse(apu);
	apu->pulse[0].timer_period = 200;
	apu->pulse[0].sweep.enable = true;
	apu->pulse[0].sweep.shift = 1;
	apu->pulse[0].sweep.reload_divider = true;
	apu->pulse[0].sweep.divider.counter = 0;
	apu->step_index = 1;
	apu->cpu_cycle_counter = 7456 * 2;
	nes_apu_clock_cpu_cycle(&apu_test_core->apu);
	APU_EXPECT_EQUAL(300, apu->pulse[0].timer_period);
	APU_EXPECT_EQUAL(0, apu->pulse[0].sweep.reload_divider);
#endif
}

int main(void)
{
	Arena arena = arena_create(0, "APU test");
	apu_test_core = arena_push_zero(&arena, sizeof(NES_Emulator));
	apu_test_reset_and_registers();
	apu_test_status_register();
	apu_test_triangle_registers();
	apu_test_triangle_counters_and_timer();
	apu_test_triangle_metadata();
	apu_test_frame_clock();
	apu_test_pulse_output_and_sweep();

	if (apu_test_failures)
	{
		fprintf(stderr, "Orbiter APU tests failed: %u failure(s)\n", apu_test_failures);
		return 1;
	}

	printf("Orbiter APU tests passed\n");
	return 0;
}
