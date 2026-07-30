## UI system

For the UI system we've settled on a model that closely follows that of RAD's debugger.

The idea is to create a transient Box tree each frame.

The Box tree represents an abstract layout. At the end of the frame, we process this
tree and generate hard geometry from it.
The key, is remembering relevant build artifacts for the next frame.

This is the essence of RAD's UI system.


We're essentially turning this:

	Box:compute_geometry()
	ui_button(Box:get_rect(), ...)

Into this:

	ui_button(Box:get_rect(), ...)
	Box:compute_geometry()


Additionally, not only is a box a node in a layout tree, but also a graphical object.
Each box contains basic paint information.

Every UI element is a Box, for instance, the thumb of a scrollbar is a Box.

Boxes can have hooks, for painting, and other hooks for integrating with the layout
system.

When it comes to the layout algorithm;

The main layout algorithm for containers, is based around Android's Linear Layout,
but extended to support shrinking weights as well as expansion weights.










The simple layout problem already solves parent child dependencies very well.
The main problem is virtual lists.
Because they introduce yet another dimension.

Here are the challenges:
	The child height is unknown, the parent viewport is unknown.
	The parent height may depend on child height and count.
	The child width and or height depends on the parent's size.
	And yet we have to have enough information to, know how big
	the scrollable range is, and know which item is currently visible.

In a retained mode UI system, you'd just know and remember child sizes. Knowing this you can
setup the scrollbar, and offset children prior to laying them out.

We're having to re-layout children because we don't know their sizes by the time we begin the
scroll area.

We did make a virtualization itself be part of the box, which makes the intended common case simpler,
but the intended common case isn't common ...

But we can't take it out either, we can't measure the child without it being in the parent, we can't
wait for the parent to layout because its size may depend on the number of children.

So it just seems like the right way to do it.

The question is, do we experiment with a RAD style one frame persisted rectangles. If we knew the
data from the previous frame, we'd know the size of the children, our rectangle, at the expense
of having one frame delay, which doesn't sound that bad.

We'd have to experiment.





----

Our current UI system is pretty much a combination of immediate mode draw calls and some helper
functions.

However, it's been getting harder and harder to scale up, and we don't even have that many
widgets.

The main problem is just overall UI management, layouts, persistent state, repetition ...

To layout things in a somewhat sensible and dynamic manner, you need to have some sort
of deffered structure that you can analyze.

And to persist information from one frame to another, you need to have some sort of temporal
cache, based off of that thing's unique identity across time.

The problem is creating a system that:
	A) Is fully dynamic
	B) Simple but scales
	C) It works
	D) It works quickly enough

Virtual lists and scrolling are the main things to lookout for.

The reason is that list items themselves would be UI elements.

So how the hell do you do scrolling if you don't already know the height of
the child item? And this is making the rather convenient simplification that
all child items have the same size.

The rad debugger uses a box tree model, they create boxes for literally _every_ _single_
thing you see that's UI. At the end of the frame, they process that tree.

So one massive tree that's taken care of at the very end.

And yet, the code still works immediate mode, acting as if it knew the rectangles of boxes
that haven't been laid out yet.

The trick is this, when a box is laid out, it remembers the rectangle.

When the box is acquired again the next frame, that's the rectangle it uses.

It's really clever and it works very well.

We might head in that direction eventually ...

Now, when it comes to virtual lists, suppose you have some parent box that acts as the
list's container, inside of it you want to place box items.

To know the size of the child box you need the size of the parent, because the parent will
impose things like paddings and other dynamic things.
And the parent itself might be within a box hierarchy, the point is, you don't know the
size of the parent at build time, so typical immediate mode code can't do anything it.
So suppose you wait for the one frame delay to get the parent's rectangle.
Now you need the size of the child item to know how many fit in and to know how which one
you're looking at.
How do you do that? You can't measure the item itself locally, you'd have to either:
	A) measure the parent locally with one item in
	B) act as the parent and measure the item yourself with the constraints

I guess, the point is, the system CAN work, but I don't think we have to adhere to this
fully immediate mode model.

I think what we can do is attach hooks to boxes, once a box is laid out, we get a callback,
in the callback we generate the children. I don't really know ...


I guess here's what we're going to do:

	Create a similar Box model.
		But instead, we're going to create localized box builders.
		We already know the size of each panel because they do their own UI layout.
		So each panel, can choose whether they want to use this system or not.
		They create a box builder, add boxes into it, at the end, the get the tree,
		lay it down, use the resulting rectangles and do immediate mode on it right
		there and then.
		For lists, we do the same thing, we lay down the parents.
		We get the rectangles, then we lay down the children.

Here are the things I want:
	A) Boxes will not dominate the entire UI but act as helpers to build render commands.

	B) We HAVE to support virtual lists out the gate, automatic overflow scroll and such.

	C) Boxes WILL NOT use linked lists for their children because I hate them and they
	make everything harder moving forward ...

	D) Simple layout model, I literally only care about linear layouts and weighted children.
	   For instance, I want to be able to set a child's weight, so that it takes up space
	   relative to the other weighted children around it.
	   I want to be able to specify the resistance to shrinking when not enough space.


