# Method

In **deai**, methods are always attached to di_object's. To create method, you call ```di_create_fn``` to create a di_fn struct, then register it with ```di_register_fn```.

The first argument a method takes is always a di_object, so it's omitted in the call to ```di_create_fn```.
