===================================
Add/Remove listener during emission
===================================

This happens more often than you thought. It's common to stop listener in dtor, and it's common for a object to be freed in a handler.

If a listener is removed before it's called during an emission, it's guaranteed to not be called. If a new listener is added during an emission, then it might or might not be called, there's no guarantee what will happen
