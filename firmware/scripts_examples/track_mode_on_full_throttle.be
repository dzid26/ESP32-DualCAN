# @name Track mode on full throttle
# @description When accelerator pedal is at 100% for >1s, request track mode ON.
#              Release pedal to cancel the request.
# @bus 0
#
# Requires: Tesla Model 3/Y dedup DBC loaded on bus 0.
#
# Signals used:
#   DI_accelPedalPos (ID 0x118, DI_torque): accelerator pedal 0-100%
#   UI_trackModeRequest (ID 0x313): 0=IDLE, 1=ON, 2=OFF
#
# This is a template — exact signal names and IDs should be verified
# against your vehicle's DBC and CAN traffic.

var full_throttle_start = 0
var track_requested = false

def setup()
  can_on("DI_accelPedalPos", def(sig)
    var pedal = sig['value']
    if pedal >= 99.0
      if !track_requested
        # TODO: need millis() to time the hold duration
        print("Full throttle detected — would request track mode")
        # var msg = can_msg_draft(0x313)
        # can_msg_set(msg, "UI_trackModeRequest", 1)  # ON
        # can_msg_send(msg)
        track_requested = true
      end
    else
      if track_requested
        print("Throttle released — cancelling track mode request")
        track_requested = false
      end
    end
  end)

  print("Track mode script loaded")
end
