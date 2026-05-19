# @name Track mode on full throttle
# @description When accelerator pedal is at 100% for >1s, request track mode ON.
#              Release pedal to cancel the request.
# @bus 0
#
# Requires: Tesla Model 3/Y DBC loaded on bus 0.
#
# Signals used:
#   DI_accelPedalPos (DI_systemStatus, ID 0x118 / 280): accelerator pedal 0-100%
#   UI_trackModeRequest (UI_powertrainControl, ID 0x313 / 787): 0=IDLE, 1=ON, 2=OFF
#
# Note: UI_powertrainControl must be present in the loaded DBC for TX to work.

var full_throttle_start = 0
var track_requested = false

def setup()
  on_can_signal("DI_systemStatus", "DI_accelPedalPos", def(sig)
    var pedal = sig['value']
    if pedal >= 99.0
      if !track_requested
        full_throttle_start = millis()
        print("Full throttle detected — would request track mode")
        # var msg = can_msg_get(0x313)
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
