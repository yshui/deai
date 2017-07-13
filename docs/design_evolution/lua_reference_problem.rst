===========
The Problem
===========

Shutting **deai** pose a problem: we need to destroy object when there're still reference to them. **deai**'s way of doing this is ``di_apoptosis``. This function will clear object's listeners and send ``__destroyed`` signal to announce its intention of being destroyed. Referrers of this object should drop the ref, eventually the object will have 0 reference, and thus be detroyed safely. This also enable objects to have a cleanup function, which user can call to initiate its destruction (e.g. ``xorg_connection.disconnect()``)

The problem is that, this object could be ref'd by a lua script. Lua scripts (and possibly other scripting languages as well) are special cases in refcount handling, because there's no way to rob a reference from lua's hand. So they can't realistically handle ``__destroyed`` signal properly.

Fortunately, if other parts of **deai** don't hold reference to any lua object, the lua state will free itself, thus release all the references. So we can have the ``lua_ref`` objects sending out ``__destroyed`` signal, so they can be freed. But it's possble that a ``lua_ref`` is ref'd by a lua script! Now we have a chicken egg problem, to free the ``lua_ref``, we need to free the lua state; to free the state, we need to free the ``lua_ref``.

============
The solution
============

The solution is a bit hacky. To break the cycle, we let ``lua_ref`` drop reference to lua state before all of its refs are gone. This left the ``lua_ref`` is a defunct state, which makes me uncomfortable, but this is the best we can do.

This also means ``lua_ref`` is probably the only object that does the same thing in its cleanup function and its dtor.
