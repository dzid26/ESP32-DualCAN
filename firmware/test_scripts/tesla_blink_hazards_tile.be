# @name Tesla flash hazards
# @description Registers a Dashboard tile that requests Tesla hazards and
#              flashes the onboard RGB LED yellow when executed.
# @bus 0
#
# Requires: Tesla Model 3 vehicle CAN DBC loaded on bus 0.
#
# TX: DAS_bodyControls, ID 0x3E9 / 1001
#   DAS_hazardLightRequest: 0=OFF, 1=ON
# This intentionally requests OFF after 3s so the flash sequence stops.

def flash_yellow(dur_ms)
  led_set(255, 255, 0)
  timer_after(dur_ms, /-> led_off())
end

def hazards_off()
  var msg = can_msg_get(0, "DAS_bodyControls")
  if msg == nil
    msg = can_msg_new("DAS_bodyControls", 0)
  end
  msg_sig_set(msg, "DAS_hazardLightRequest", 0)
  can_msg_send(0, msg)
  print("Hazards off requested")
end

def hazards_on()
  flash_yellow(150)

  var msg = can_msg_get(0, "DAS_bodyControls")
  if msg == nil
    msg = can_msg_new("DAS_bodyControls", 0)
  end
  msg_sig_set(msg, "DAS_hazardLightRequest", 1)
  can_msg_send(0, msg)
  print("Hazards on requested")

  timer_after(3000, /-> hazards_off())
end

def setup()
  action_register("blink hazards", /-> hazards_on())
  print("Tesla hazards tile loaded")
end
