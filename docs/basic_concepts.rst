==============
Basic Concepts
==============

At its core, deai is an object system, not dissimilar to GLib's GObject system, albeit with a very different design.

Objects
=======

Rather than being the "object" in Object Oriented Programming, an object in deai is closer to a Python or Javascript object, or a Lua table. This design choice was made to simplify interfacing with scripting languages.

In other words, an object is just a dictionary mapping string keys to its members. There is some caveats, but from a users perspective that's mostly it.

Using Lua as an example, say you get a deai object:

.. code-block:: lua

   obj = di.xorg -- get the xorg module

Then you can use it like this:

.. code-block:: lua

   a = obj.some_attribute -- read a member of the object
   obj.some_attribute = a + 1 -- write to a member of the object
   obj:connect() -- connect to the X server

Signals
=======

Signals are how events are delivered in deai. Signals are emitted from objects, and can carry arguments with them.

Again using Lua as an example, say you have an object :code:`obj`:

.. code-block:: lua

   obj:on("some-signal", function(data)
     -- do something with data
   end)

This register a listener on the event :code:`"some-signal"`. This signal carries an argument :code:`data`, which will be passed on to the callback function.

Language support
================

Scripting language supports in deai are implemented as plugins. Currently there is only the Lua plugin, but I will use plural anyways.

These plugins implement the ability to load a script, and they also exposes deai objects in the languages' native form.

For more information on, for example, how Lua scripting support works, see :lua:mod:`lua`.

Compiled language support is implemented as bindings. C is supported by core deai, there is also C++ binding available.
