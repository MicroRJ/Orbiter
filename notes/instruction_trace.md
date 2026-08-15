# CPU Instruction Tracing

## Current model

The emulator no longer owns a persistent circular instruction trace.
The debugger allocates a linear, frame-sized buffer once and passes it into
emulator run calls.

One PC sample is written for every scheduler step. The sample contains the CPU
address, mapped address, and byte. Its array index is its scheduler-step offset
from the pre-run debugger snapshot, so trace records do not store a clock.

An interrupt-only step may sample a PC whose instruction does not execute.
This is intentional: the trace records observed PC state per scheduler step rather
than retired instructions.

If this ever materially affects program hit counts or execution edges, revisit it then.

A 16,384-entry debugger buffer is enough for a frame under the current scheduler.
At 12 bytes per entry, it uses 192 KiB.

## Previous measurements

The previous design packed 24-byte records into 8 bytes in a 512 KiB circular buffer.
Packing produced no measurable emulator improvement and added a small debugger decoding cost.
Splitting the ring into two spans recovered some decode traversal cost, but both packing and
span traversal became unnecessary after moving to the caller-owned linear buffer.
