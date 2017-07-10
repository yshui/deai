# Who frees what?

Arguments and return values are all freed by the caller. Which means when a function choose to return a DI\_TYPE\_STRING, it should make sure that pointer is safe to pass to free()
