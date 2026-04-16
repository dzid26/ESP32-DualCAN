#pragma once

#include "berry.h"
#include "can/can_engine.h"

/* Set the engine instances that Berry bindings operate on.
 * Must be called before berry_register_bindings(). */
void berry_set_engines(can_engine_t *eng0, can_engine_t *eng1);

/* Register all native functions into the Berry VM. */
void berry_register_bindings(bvm *vm);
