#pragma once
#include "ModuleOrder.h"
#include "ModuleStackCore.h"
namespace ava::module_stack {
inline Order initial() { return initial(module_order::defaultOrder); }
}
