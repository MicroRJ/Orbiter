
#define TIMELINE_SIZE (1024*16)
#define TIMELINE_MASK (TIMELINE_SIZE - 1)

struct
{
	u32         id;
	Prof_Frame  timeline[TIMELINE_SIZE];
	u64         timeline_index;
}
thread_decl Prof;

static f64 prof_seconds_now(void)
{
	static u64 frequency;
	if (!frequency) frequency = platform_counter_frequency();
	return frequency ? platform_counter() / (f64)frequency : 0.0;
}

i64 prof_timeline_index() {
	return Prof.timeline_index;
}

Prof_Frame *prof_timeline_frame(i64 index) {
	if (index < 0) index += Prof.timeline_index + 1;
	return & Prof.timeline[index & TIMELINE_MASK];
}

Prof_Frame *prof_frame() {
	return & Prof.timeline[Prof.timeline_index & TIMELINE_MASK];
}

void prof_begin_frame() {
	memory_zero(prof_frame(), sizeof(* prof_frame()));
	prof_frame()->id = Prof.timeline_index;
	prof_frame()->time.seconds -= prof_seconds_now();
}

void prof_close_frame()
{
	prof_frame()->time.seconds += prof_seconds_now();
	Prof.timeline_index ++;
}

void prof_add_metric(Prof_Metric metric, i64 size) {
	prof_frame()->metrics.fields[metric] += size;
}

void prof_begin_scope(Prof_Scope *scope, String name) {
	Assert(name.data);
	Assert(name.size);
	if (scope->id == 0) {
		Assert(Prof.id < ArrayCount(prof_frame()->fields));
		scope->id = ++ Prof.id;
	}
	prof_frame()->nfields = Max(prof_frame()->nfields, scope->id);
	prof_frame()->fields[scope->id - 1].freq ++;
	prof_frame()->fields[scope->id - 1].name = name;
	prof_frame()->fields[scope->id - 1].time.seconds -= prof_seconds_now();
}

void prof_close_scope(Prof_Scope *scope) {
	Assert(scope->id > 0);
	prof_frame()->fields[scope->id - 1].time.seconds += prof_seconds_now();
}
