===============
Weak ref design
===============

An object can hold a reference to another object without incrementing its reference count.

One use case can be a cache system. For example, the xorg connection object can hold weak reference to its sub objects.

In order to handle the destroy of the weakly ref'd object, the referrer needs to handle the ``__destroy`` signal, which is triggered when an object is being freed. Or the referee needs to remove all the weak refs to it when it dies.

**Weakly capturing closure doesn't handle this**. The creator of the closure has the responsibility.

======================
Necessary ref cycles
======================

One example is the listener. A listener signifies the fact that some one is interested in certain signal emitted by an object. Which means that object need to be kept alive.

This normally is not a problem. Because in order to emit signals from an object, some one has to hold a reference to it, which keeps the object alive. This reference is usually held by an upstream listener.

But this has the problem that, an object will be kept alive even when no one is interested in its signals. So, it might be a good idea to **emit signal through weak references of an object**.

The bigger problem is that, in order for the listener to keep the object alive, their need to be a listener -> object (1) reference. But there's normally a object -> signal -> listener (2) reference, causing a cycle.

The (2) reference is necessary, because it keeps the listener alive when there's not external references. Listener should only die when it's requested by the user, or when upstream dies.

So (1) and (2) combined seems to be a **unavoidable cycle**.

Ideas
-----

- It's probably fine during normal execution of the program. Object will at least be reachable through the weak reference

- Probably a problem during shutdown: All reference to object will go away, memory leaked

- So the object needs to handle the "shutdown" signal to clear itself (Should the shutdown handler closure be weak?)

- Problem: this forces all the signal emitters to handle "shutdown" signal. Maybe let the emitter choose if the listener should hold ref of it.

Decision
--------

1. Remove the object -> member -> signal indirection. Now signals belongs directly to the object. No need to register signals.

2. All objects capable of generation signals, should:

  a) listen on the "shutdown" signal (could be indirectly, if order of freeing is required).

  b) Add a "detach" member to the upstream listener, if the source of signal is from some upstream. This member will be called if the listener is stopped, or cleared by upstream

3. Object should clear its listeners in the shutdown handler/detach
