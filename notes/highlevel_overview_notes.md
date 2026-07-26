Hi all.
Here's what you're looking at:
Top right: Shows the program disassembly (of a NES ROM) view as it tries to keep up with
the CPU's position.
Bottom left, the grid thing:
Shows the program rom & ram (where are the instructions live) as a grid of cells.
Each cell is mapped to a block of memory, in the video it uses 64 bytes per cell,
then 32, you can zoom in to 4 bytes per cell or go all the way up to 256 bytes per cell.


The wires:
Orbiter tracks the execution path of the CPU in the form of a transition edge.

Frequented edges are bright, but recently stimulated edges spike briefly and
decay quickly unless stimulated.

The idea is to be able to tell what's been active from what just happened.
For instance, when you press a key you can see some of the wires light up.

Bottom right, that shows the profiler graph.


The "rewind" effect:

Every frame (17 ms) I create a snapshot of the entire emulator's hard state and push it
to a circular buffer.

It doesn't have to be every frame, could be every 100 ms, it's just arbitrary at this point.

Now, this allows me to do two things:

A) I can rewind backwards and forwards, which is cool ...

B) Allows me to handle breakpoints.

For instance, suppose you want to place a breakpoint at instruction X.

What I do is as follows: Run the emulator as you normally would, if I detect a breakpoint
was triggered, go back in time to the previous snapshot, currently 17 ms back in time.

Then, rerun the emulator to the exact point right _before_ the breakpoint triggered.

If you're wondering, even with my unoptimized emulator, replaying 1 second worth of gameplay
is only around 300 ms.

Since our snapshots are roughly every 17 ms, rerunning the emulator is practically instantaneous.

Not sure if other emulators do it this way, but it just seems like a really cool way to do it.

I do plan to implement compression so that I can record for much longer, but there's a lot more
work left to do xD ...

GitHub => MicroRJ\Orbiter