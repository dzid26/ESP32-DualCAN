#pragma once

#include "berry.h"
#include "can/can.h"

void berry_set_buses(can_t *bus0, can_t *bus1);
void berry_register_bindings(bvm *vm);
