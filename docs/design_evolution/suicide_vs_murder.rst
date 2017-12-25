=================
Suicide vs Murder
=================


So there's two reasons for deai to exit:

  * Nothing is going on. Meaning there're no listeners installed. Nothing can happen is this case, so deai should quit

  * User exits. User wants to quit deai, there might or might not be listeners active.

This first case is handle like this:

There is a "core" object, deai will quit if the reference count of this object drops to 0. Only listeners of some event and the "core" event sources can hold reference of this object. When all listeners are gone, all event sources will be gone too (hopefully, if there's no bug). Then deai will quit. This is aa bottom up self destruction ("suicide").

This second case requires a top down approach. This case also happens at a smaller scale: e.g. when user closing a xorg connection, before its refcount drops to 0.

This requires objects to implement a "destroy" method, which is kind of a subset of dtor. This "destroy" method will be used in two cases:

  * User initiated destruction of the object

  * When the event sources this object listen to dies

The second case is important because the top down destruction ("murder") depends on it

The "destroy" method should at least remove all listeners, and notify all listeners of the destroyed object. Often doing this is enough to trigger the dtor of objects (are there cases it doesn't?).

This leads to code duplication: dtor might be called before or after the event sources die, so it must handle two cases, which is annoying

.. Not relevant? :
   Because a defuct object still needs to serve method calls, those method calls needs to handle a defuct object properly, and return errors when approperiate. 
   It might be a good idea to just remove all members from a defuct object.


