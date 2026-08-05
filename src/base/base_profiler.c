
#define PROF_TIMELINE_MASK (PROF_TIMELINE_CAPACITY - 1)

struct
{
	Str         field_names[PROF_MAX_SCOPES];
	u64         next_field_index;
	Prof_Frame  timeline[PROF_TIMELINE_CAPACITY];
	u64         timeline_index;
	Prof_Frame  frame;
	Prof_Field  totals[PROF_MAX_SCOPES];
}
thread_decl Prof;

static f64 prof_seconds_now(void)
{
	static u64 frequency;
	if (!frequency) frequency = platform_counter_frequency();
	return frequency ? platform_counter() / (f64)frequency : 0.0;
}

Str prof_get_field_name(u32 index) {
	Assert(index < PROF_MAX_SCOPES);
	return Prof.field_names[index];
}

Prof_Field prof_get_total(u32 index) {
	Assert(index < PROF_MAX_SCOPES);
	return Prof.totals[index];
}

u64 prof_timeline_cursor() {
	return Prof.timeline_index;
}

const Prof_Frame *prof_timeline_frame(u64 index) {
	return & Prof.timeline[index & PROF_TIMELINE_MASK];
}

void prof_begin_frame() {
	memory_zero(& Prof.frame, sizeof(Prof.frame));
	Prof.frame.id = Prof.timeline_index;
	Prof.frame.time -= prof_seconds_now();
}

void prof_close_frame()
{
	Prof.frame.time += prof_seconds_now();
	Prof.timeline[Prof.timeline_index ++ & PROF_TIMELINE_MASK] = Prof.frame;
	for (u32 i = 0; i < Prof.frame.nfields; ++ i)
	{
		Prof.totals[i].freq += Prof.frame.fields[i].freq;
		Prof.totals[i].time += Prof.frame.fields[i].time;
	}
}

void prof_add_metric(Prof_Metric metric, i64 size) {
	Prof.frame.metrics.fields[metric] += size;
}

void prof_begin_scope(Prof_Scope *scope, Str name) {
	Assert(name.data);
	Assert(name.size);
	if (!scope->has_field_index) {
		Assert(Prof.next_field_index < ArrayCount(Prof.frame.fields));
		u32 field_index = Prof.next_field_index ++;
		Prof.field_names[field_index] = name;
		scope->field_index = field_index;
		scope->has_field_index = true;
	}
	Prof.frame.nfields = Max(Prof.frame.nfields, scope->field_index + 1);
	Prof.frame.fields[scope->field_index].freq ++;
	Prof.frame.fields[scope->field_index].time -= prof_seconds_now();
}

void prof_close_scope(Prof_Scope *scope) {
	Assert(scope->has_field_index);
	Prof.frame.fields[scope->field_index].time += prof_seconds_now();
}
