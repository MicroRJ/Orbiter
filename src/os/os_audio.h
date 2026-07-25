#ifndef OS_AUDIO_H
#define OS_AUDIO_H

#include "base.h"

typedef struct
{
	u32 buffer_frame_count;
	u32 sample_rate;
	u32 channel_count;
}
OS_AudioInfo;

b32 os_audio_init(OS_AudioInfo *info);
void os_audio_shutdown(void);
u32 os_audio_writable_frames(void);
u32 os_audio_write_mono(const f32 *samples, u32 count);

#endif
