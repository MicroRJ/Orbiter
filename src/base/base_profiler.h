
#define PROF_METRICS_XDEF(_) \
_(METRIC_STACK_PUSH_CALLS        ,           "stack_push_calls"   ) \
_(METRIC_STACK_ALIGN_CALLS       ,          "stack_align_calls"   ) \
_(METRIC_STACK_PUSH_SIZE         ,            "stack_push_size"   ) \
_(METRIC_HASH_TABLE_MISSES       ,          "hash_table_misses"   ) \
_(METRIC_HASH_TABLE_LOOKUPS      ,         "hash_table_lookups"   ) \
_(METRIC_HASH_FUNC_CALLS         ,            "hash_func_calls"   ) \
_(METRIC_HASH_BYTES              ,            "hash_bytes"        ) \
_(METRIC_TEXT_LAYOUT_CALLS       ,          "text_layout_calls"   ) \
_(METRIC_TEXT_LAYOUT_BYTES       ,          "text_layout_bytes"   ) \
_(METRIC_BUILD_TEXT_RUN_CALLS    ,       "build_text_run_calls"   ) \
_(METRIC_DRAW_RUNS               ,               "draw_runs"      ) \
_(METRIC_DRAW_RUN_BYTES          ,          "draw_run_bytes"      ) \
_(METRIC_DRAW_COMMANDS           ,           "draw_commands"      ) \
_(METRIC_DRAW_COMMAND_BYTES      ,     "draw_command_bytes"       ) \
_(METRIC_DRAW_RECT_COMMANDS      ,      "draw_rect_commands"      ) \
_(METRIC_DRAW_IMAGE_COMMANDS     ,     "draw_image_commands"      ) \
_(METRIC_DRAW_TEXT_COMMANDS      ,      "draw_text_commands"      ) \
_(METRIC_DRAW_EFFECT_COMMANDS    ,    "draw_effect_commands"      ) \
_(METRIC_DRAW_CLIPPED_COMMANDS   ,   "draw_clipped_commands"      ) \
_(METRIC_DRAW_EMISSIVE_COMMANDS  ,  "draw_emissive_commands"      ) \
_(METRIC_DRAW_COMMAND_REPLAYS    ,    "draw_command_replays"      ) \
_(METRIC_DRAW_BACKDROP_BLURS     ,     "draw_backdrop_blurs"      ) \
_(METRIC_DRAW_BLOOM_SLICES       ,       "draw_bloom_slices"      ) \
_(METRIC_DRAW_PASSES             ,              "draw_passes"     ) \
_(METRIC_DRAW_BATCHES            ,             "draw_batches"     ) \
_(METRIC_DRAW_CALLS              ,               "draw_calls"     ) \
_(METRIC_DRAW_INSTANCES          ,           "draw_instances"     ) \
_(METRIC_DRAW_INSTANCE_BYTES     ,       "draw_instance_bytes"    ) \
_(METRIC_DRAW_BATCH_BREAKS       ,        "draw_batch_breaks"     ) \
_(METRIC_DRAW_BATCH_PASS_ENDS    ,      "draw_batch_pass_ends"    ) \
_(METRIC_DRAW_BATCH_TEXTURE      , "draw_batch_texture_changes"   ) \
_(METRIC_DRAW_BATCH_SCISSOR      , "draw_batch_scissor_changes"   ) \
_(METRIC_DRAW_BATCH_SAMPLER      , "draw_batch_sampler_changes"   ) \
_(METRIC_DRAW_BATCH_BLENDER      , "draw_batch_blender_changes"   ) \
_(METRIC_DRAW_BATCH_SHADER       ,  "draw_batch_shader_changes"   ) \
_(METRIC_DRAW_BATCH_TEXTURE_MODE ,    "draw_batch_mode_changes"   ) \
_(METRIC_DRAW_BATCH_SHADER_BLOCK ,   "draw_batch_block_changes"   ) \
_(METRIC_COPY_MEMORY_CALLS       ,       "copy_memory_calls"      ) \
_(METRIC_COPY_MEMORY_SIZE        ,       "copy_memory_size"       ) \
_(METRIC_ZERO_MEMORY_CALLS       ,       "zero_memory_calls"      ) \
_(METRIC_ZERO_MEMORY_SIZE        ,       "zero_memory_size"       ) \
_(METRIC_FILL_MEMORY_CALLS       ,       "fill_memory_calls"      ) \
_(METRIC_FILL_MEMORY_SIZE        ,       "fill_memory_size"       ) \
_(METRIC_AUDIO_SAMPLES_GENERATED ,      "audio_samples_generated" ) \
_(METRIC_CPU_CYCLES              ,                   "cpu_cycles" ) \
_(METRIC_PPU_SCANLINES           ,                   "ppu_cycles" ) \
_(METRIC_PPU_VBLANKS             ,                  "ppu_vblanks" ) \
/* end */

typedef enum
{
#define XPAND(NAME, REP) PROF_##NAME,
	PROF_METRICS_XDEF(XPAND)
#undef XPAND
	PROF_METRIC_COUNT_,
}
Prof_Metric;

typedef struct
{
	i64 fields[PROF_METRIC_COUNT_];
}
Prof_MetricBlock;

#define PROF_MAX_SCOPES 32

typedef struct
{
	u32 id;
}
Prof_Scope;

typedef struct
{
	String     name;
	Seconds    time;
	u32        freq;
}
Prof_Field;

typedef struct
{
	Prof_MetricBlock metrics;
	u64                  id;
	Seconds             time;
	u32              nfields;
	Prof_Field        fields[PROF_MAX_SCOPES];
}
Prof_Frame;

// Roughly 4.5 minutes before circling back.
#define PROF_TIMELINE_CAPACITY (64 * 256)
STATIC_ASSERT(!(PROF_TIMELINE_CAPACITY & (PROF_TIMELINE_CAPACITY - 1)));


void prof_begin_frame();
void prof_close_frame();
u64 prof_timeline_cursor();
const Prof_Frame *prof_timeline_frame(u64 index);
void prof_begin_scope(Prof_Scope *scope, String name);
void prof_close_scope(Prof_Scope *scope);

#define PROF_BLOCK_AUTO(SCOPE, LABEL) \
	static Prof_Scope SCOPE;           \
	for (u32 done = (prof_begin_scope(&SCOPE, LABEL), 0); !done; prof_close_scope(&SCOPE), done = 1)

#define PROF_BLOCK(LABEL) PROF_BLOCK_AUTO(CONCAT(prof_scope_,__COUNTER__), LIT(LABEL))

void prof_add_metric(Prof_Metric metric, i64 add);
