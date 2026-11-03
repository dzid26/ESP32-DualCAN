#pragma once

/* Bump when the Berry C binding set changes incompatibly (add/remove/change
 * a be_regfunc call in berry_bindings.c or berry_registry.c). The web UI warns
 * before uploading scripts validated against a different version. */
#define SCRIPTING_API_VERSION 1

#include "berry.h"
#include "can/can.h"
#include "scripting/berry_log.h"
#include "scripting/berry_registry.h"

void berry_set_buses(can_t *bus0, can_t *bus1);
/* Returns NULL for an out-of-range or unconfigured index. */
can_t *berry_get_bus(int bus);
void berry_register_bindings(bvm *vm);
