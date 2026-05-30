# @name Tesla fold mirrors
# @description Registers Dashboard tiles to fold/unfold Tesla mirrors. Each
#              tile blinks the onboard RGB LED blue when executed.
# @bus 0
#
# Requires: Tesla Model 3/Y dedup DBC loaded on bus 0.
#
# TX: UI_vehicleControl, ID 0x273 / 627
#   UI_mirrorFoldRequest: 0=IDLE, 1=RETRACT, 2=PRESENT
# This injects a one-shot request. Do not send a follow-up IDLE frame.

def flash_blue(dur_ms)
  led_set(0, 0, 40)
  timer_after(dur_ms, /-> led_off())
end

def mirror_request(value, label)
  flash_blue(150)

  var msg = can_msg_get("UI_vehicleControl", 0)
  if msg == nil
    print("Mirror " .. label .. " skipped: UI_vehicleControl not available")
    return
  end

  can_msg_set(msg, "UI_mirrorFoldRequest", value)
  can_msg_send(msg)
  print("Mirror " .. label .. " requested")
end

def setup()
  action_register("fold_mirrors", /-> mirror_request(1, "fold"))
  action_register("unfold_mirrors", /-> mirror_request(2, "unfold"))
  print("Tesla mirror tiles loaded")
end
