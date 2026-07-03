# @name Track mode on full throttle
# @description When accelerator pedal is at 100% for >1s, request track mode ON.
#              Release pedal to cancel the request.
# @bus 0
#
# Requires: Tesla Model 3/Y DBC loaded on bus 0.
#
# Signals used:
#   DI_accelPedalPos (DI_systemStatus, ID 0x118 / 280): accelerator pedal 0-100%
#   UI_trackModeRequest (UI_trackModeSettings, ID 0x313 / 787): 0=IDLE, 1=ON, 2=OFF
#
# Note: UI_trackModeSettings must be present in the loaded DBC for TX to work.

var TRACK_REQ_IDLE = 0
var TRACK_REQ_ON = 1
var TRACK_REQ_OFF = 2

var TRACK_STATE_UNAVAIL = 0
var TRACK_STATE_AVAIL = 1
var TRACK_STATE_ON = 2

var TRACK_STATE_NAMES = ["UNAVAILABLE", "AVAILABLE", "ON"]

var THROTTLE_THRESHOLD = 248    # raw * 0.4 = 99.2% (allow small tolerance)
var HOLD_MS = 1000
var POLL_MS = 100

var throttle_start_ms = 0
var track_mode_sent = false
var prev_track_state = -1

def poll()
  var sys = can_msg_get(0, "DI_systemStatus")
  if sys == nil
    return
  end

  var pedal_raw = msg_sig_get(sys, "DI_accelPedalPos")
  if pedal_raw == nil || pedal_raw >= 255
    return
  end

  var now = millis()

  if pedal_raw >= THROTTLE_THRESHOLD
    if throttle_start_ms == 0
      throttle_start_ms = now
    elif !track_mode_sent && (now - throttle_start_ms) >= HOLD_MS
      print("Full throttle held for 1s — requesting Track Mode ON")
      var req = can_msg_new("UI_trackModeSettings")
      if req != nil
        msg_sig_set(req, "UI_trackModeRequest", TRACK_REQ_ON)
        can_msg_send(0, req)
        track_mode_sent = true
      end
    end

    var track_state = msg_sig_get(sys, "DI_trackModeState")
    if track_state != nil && track_state != prev_track_state
      var name = TRACK_STATE_NAMES[track_state]
      if name == nil name = "?" end
      print("Track mode state: " .. name)
      prev_track_state = track_state
    end
  elif throttle_start_ms != 0
    throttle_start_ms = 0
    prev_track_state = -1
  end
end

def setup()
  timer_every(POLL_MS, poll)
  print("Track mode on full throttle loaded")
end
