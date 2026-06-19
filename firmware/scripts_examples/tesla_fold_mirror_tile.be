# @name Tesla fold mirrors
# @description Registers Dashboard tiles to fold/unfold Tesla mirrors.
# @bus 0
#
# TX: UI_vehicleControl, ID 0x273 / 627
#   UI_mirrorFoldRequest: 0=IDLE, 1=RETRACT, 2=PRESENT

def fold()
  print("=== fold ===")
  var msg = can_msg_get("UI_vehicleControl", 0)
  print("  can_msg_get -> " .. str(msg))
  if msg == nil
    msg = can_msg_new("UI_vehicleControl", 0)
    print("  can_msg_new -> " .. str(msg))
  end
  print("  msg type: " .. type(msg))
  can_msg_set(msg, "UI_mirrorFoldRequest", 1)
  print("  can_msg_set OK")
  can_msg_send(0, msg)
  print("  can_msg_send OK")
  led_set(0,0,40)
  timer_after(150, /-> led_off())
end

def unfold()
  print("=== unfold ===")
  var msg = can_msg_get("UI_vehicleControl", 0)
  if msg == nil
    msg = can_msg_new("UI_vehicleControl", 0)
  end
  can_msg_set(msg, "UI_mirrorFoldRequest", 2)
  can_msg_send(0, msg)
  led_set(0,0,40)
  timer_after(150, /-> led_off())
end

def setup()
  action_register("fold_mirrors", /-> fold())
  action_register("unfold_mirrors", /-> unfold())
  print("Tesla mirror tiles loaded")
end
