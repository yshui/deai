#pragma once

#include <deai/deai.h>

/// Get the deai object from a di_module
PUBLIC_DEAI_API struct deai *di_module_get_deai(struct di_module *);
