# Live Instruction Tracing

Packed trace instructions are now one-third the original size, going from 24
bytes to 8 bytes.

There is no measurable or significant overall performance difference. The only
visible cost is a few fractions of a millisecond added by the debugger decoding
the packed instructions. We will need to investigate whether we can make that
debugger decoding faster.

Added a helper that splits the scheduler trace into two contiguous spans. This
recovered some of the performance lost by going through the indexed trace
accessor for every instruction. The debugger can now walk both spans directly
without repeating the ring-buffer bounds check and masked address calculation
on every iteration.
