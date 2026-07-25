# Notes on the incremental program discovery process

Since the beginning of this project, my main problem was the disassembly view.

Here's the problem.

The NES has a fixed, read-only program rom, or program memory, that's where all
the instructions reside.

Sure; just parse that and display it.

Not quite, instructions are variable length.

Sure; just start at the beginning, and decode each instruction and advance by
the instruction size.

Not quite, we don't know where the instructions actually start within the rom.

Ok, surely there's gotta be a way to figure out where the game has the instructions
stored.

Nope, you have to discover it.

Ok, just remember the places the CPU has executed, and remember those as instructions.


Closer, but ... you can't quite do that, because the CPU switches banks, one moment
it's looking at one region of the ROM, the next it's looking at another.

So a map from CPU address -> PRG address is fragile at the very least.

And that's the entire problem.

We don't know anything, and we have to build persistent knowledge from a transient
moving window, we also have to guess instructions to fill in the blank space and
patch instructions as the program executes and finds new ones.

In summary, the problem boils down to: refining global knowledge from brief insights.

So here's a step by step breakdown of how I ended up arriving at the current solution:

First, build a parallel array, a 1:1 map of the program rom, and that map just tells us
whether that offset is the start of an instruction or not.

Having this is already good enough for self correcting instructions, because what
we're going to do next is:

Every frame, rescan the entire CPU address space, and decode the instructions.
My design allows me to get a mapped address from any read without causing side-effects
So if I do a CPU read I know where it lands anywhere on the NES, whether on the
program read only memory (ROM) program random access memory (RAM).

Scanning the CPU address space every frame has a nice property that it works even with
bank switching, so you're always looking at the right thing.

The point is, every frame, we decode the entire CPU's view as instructions.

Here's the self correcting part:

As we're decoding, starting at zero, if the byte indicates that this instruction would cross over
a known instruction address, we know that guess was wrong, so we don't mark these bytes as an
instruction, instead you list them as individual "???".
Then, you re-sync with the known instruction offset and continue scanning.

At the end you end up with an accurate program listing for that frame, and as we discover more
and more instruction the program will self correct more accurately.

The hard part is making this not take 2 ms of your frame time.
In -O2 though, this is probably nothing, but as always, you want something better.


Now, the next step is to realize that you're probably doing this:

	Program_Listing pl := build_program_listing_by_scanning_entire_cpu_address_space(...)

	function_that_uses_instruction_count_to_draw_a_scrollbar(lp.instruction_count)

	function_that_draws_a_slice_of_the_program_listing(pl, start_index, visible_instructions)


Note that in one hand, you need the number of instruction so you can do scrolling accurately.
On the other one, you need a slice of the program listing to actually draw the instructions.

The key is realizing that you can actually decouple getting the instruction count from getting
the program slice. And this allows you to not have to know everything at once.

And furthermore, with this approach you don't have to cache anything.

What you do is figure out a way to:
	1) Count CPU visible instructions
	2) Get an instruction slice


...
Todo, complete this document!
