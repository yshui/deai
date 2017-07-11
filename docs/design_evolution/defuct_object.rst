=============
Defuct Object
=============

Sometimes it makes sense to let the user destroy an object before ref_count drop to 0. e.g. closing a xorg connection.

It's likely that this destroy method will do similar thing as the dtor. It often makes sense to make these two the same function, but you should be careful to not do destructor multiple times. You also need to remove all listeners in this function.

Because a defuct object still needs to serve method calls, those method calls needs to handle a defuct object properly, and return errors when approperiate.

It might be a good idea to just remove all members from the object as a solution.
