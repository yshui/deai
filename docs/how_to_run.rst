================
Running a Script
================

deai has a somewhat convoluted command line interface that might look weird on first sight. To run a script, you would do:

.. code-block:: bash

   deai lua.load_script s:/path/to/script.lua

Since deai is a scripting system, all aspects of it can be manipulated from the script, there is no need to have complex switches on the command line interface. Yet the command line still need some flexibility, since there are plans to add support for more scripting languages in the future. The most straightforward way is to expose the modules and methods directly on the command line interface.

Essentially, the :code:`deai` command allows you to call a method provided by deai, in the above example, the method called is :code:`lua.load_script`, the rest of the command line arguments are arguments to this method. Each argument is prefixed with a "type tag", since otherwise deai won't know how to interpret them. Here is a list of type tags:

======== ======= =======
Type tag   Type  Example
======== ======= =======
s:       string  s:TEXT
i:       integer i:1
f:       float   f:1.0
======== ======= =======
