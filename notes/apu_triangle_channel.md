Triangle Channel:

Relevant registers:
	$4015: ---D NT21
		T - enable / disable triangle channel

		** Writing a zero to any of the channel enable bits (NT21) will silence that
		   channel and halt its length counter. **


	$4008: CRRR RRRR WRITE
		C - control flag
		  - this bit is also the length counter halt flag
		R - counter reload value

	$400A: LLLL LLLL WRITE
		L - timer low

	$400B: LLLL LHHH WRITE
		L - length counter load
		H - timer high

The triangle channel maintains:
	- a length counter
	- a length counter halt flag, aliased as control flag
	- a linear counter
	- a linear counter reload flag
	- a sequencer
	- timer_period: 11 bits
	- timer:        cycles from timer_period to 0 (ticks at the rate of the CPU)
	                clocks the waveform generator when it goes from 0 to timer_period. (???)


The timer clocks the sequencer if _both_ the length counter and the linear counter
are nonzero.

The frame counter clocks the linear counter every quarter frame.

```
function linear_counter_clock()
{
	if (reload_linear_counter_flag != 0) {
		linear_counter = counter_reload_value
	}
	else if (linear_counter != 0) {
		linear_counter --;
	}
	if (control_flag == 0) {
		reload_linear_counter_flag = 0
	}
}
```






