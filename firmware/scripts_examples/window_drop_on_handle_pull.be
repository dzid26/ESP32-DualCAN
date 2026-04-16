# @name Window drop on long handle pull
# @description When the door is open and the handle is pulled for >2s,
#              send a window-down command for that door.
# @bus 0
#
# Requires: Tesla Model 3 vehicle DBC loaded on bus 0.
#
# Signals used (from VCRIGHT_doorStatus, ID 0x103):
#   VCRIGHT_frontHandlePulled        bit 10, 1 bit
#   VCRIGHT_frontLatchStatus         bit 0, 4 bits (0=closed, 1+ = open states)
#
# TX: Window command would go to the appropriate body controller.
#     The exact message ID and signal for "lower window" needs to be
#     reverse-engineered. This script is a template showing the pattern.

var handle_start_ms = 0
var handle_held = false

def setup()
  can_on("VCRIGHT_frontHandlePulled", def(sig)
    if sig['value'] > 0
      if !handle_held
        handle_held = true
        handle_start_ms = 0  # TODO: need millis() binding
        print("Handle pulled — timing...")
      end
    else
      if handle_held
        handle_held = false
        print("Handle released")
      end
    end
  end)

  can_on("VCRIGHT_frontLatchStatus", def(sig)
    if sig['value'] > 0
      print("Door open, latch=" .. str(sig['value']))
    end
  end)

  print("Window drop script loaded")
end

def teardown()
  print("Window drop script disabled")
end
