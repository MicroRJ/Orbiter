
#define PROF_TIMELINE_MASK (PROF_TIMELINE_CAPACITY - 1)

struct
{
	u32         id;
	Prof_Frame  timeline[PROF_TIMELINE_CAPACITY];
	u64         timeline_index;
	Prof_Frame  frame;
}
thread_decl Prof;

static f64 prof_seconds_now(void)
{
	static u64 frequency;
	if (!frequency) frequency = platform_counter_frequency();
	return frequency ? platform_counter() / (f64)frequency : 0.0;
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
	Prof.frame.time.seconds -= prof_seconds_now();
}

void prof_close_frame()
{
	Prof.frame.time.seconds += prof_seconds_now();
	Prof.timeline[Prof.timeline_index ++ & PROF_TIMELINE_MASK] = Prof.frame;
}

void prof_add_metric(Prof_Metric metric, i64 size) {
	Prof.frame.metrics.fields[metric] += size;
}

void prof_begin_scope(Prof_Scope *scope, String name) {
	Assert(name.data);
	Assert(name.size);
	if (scope->id == 0) {
		Assert(Prof.id < ArrayCount(Prof.frame.fields));
		scope->id = ++ Prof.id;
	}
	Prof.frame.nfields = Max(Prof.frame.nfields, scope->id);
	Prof.frame.fields[scope->id - 1].freq ++;
	Prof.frame.fields[scope->id - 1].name = name;
	Prof.frame.fields[scope->id - 1].time.seconds -= prof_seconds_now();
}

void prof_close_scope(Prof_Scope *scope) {
	Assert(scope->id > 0);
	Prof.frame.fields[scope->id - 1].time.seconds += prof_seconds_now();
}
