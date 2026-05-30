# @name Tesla gear LED demo
# @description Polls live vehicle signals with can_signal_get every 250 ms and
#              changes the onboard RGB LED colour by gear (P=white, R=red,
#              N=yellow, D=green). Also registers a tile to flash hazards.
# @bus 0
#
# Requires: Tesla Model 3/Y DBC loaded on bus 0.
#
# Reads from DI_systemStatus (ID 0x118 / 280) via can_signal_get:
#   DI_gear:         1=P  2=R  3=N  4=D
#   DI_accelPedalPos: 0-100 %
#
# TX via DAS_bodyControls (ID 0x3E9 / 1001):
#   DAS_hazardLightRequest: 0=OFF  1=ON

def gear_led(gear)
  if gear == 1       # P — park
    led_set(20, 20, 20)
  elif gear == 2     # R — reverse
    led_set(40, 0, 0)
  elif gear == 3     # N — neutral
    led_set(20, 20, 0)
  elif gear == 4     # D — drive
    led_set(0, 40, 0)
  else
    led_off()
  end
end

def poll()
  var gear_sig  = can_signal_get("DI_systemStatus", "DI_gear", 0)
  var pedal_sig = can_signal_get("DI_systemStatus", "DI_accelPedalPos", 0)

  if gear_sig == nil
    return  # DBC not loaded or no frames seen yet
  end

  gear_led(int(gear_sig['value']))

  if pedal_sig != nil && pedal_sig['changed']
    var pct = int(pedal_sig['value'])
    if pct > 5
      print("Pedal " .. str(pct) .. "%  gear=" .. str(int(gear_sig['value'])))
    end
  end
end

def hazards_on()
  led_set(255, 200, 0)
  timer_after(150, /-> led_off())

  var msg = can_msg_get("DAS_bodyControls", 0)
  if msg == nil
    print("Hazards: DAS_bodyControls not found in DBC")
    return
  end
  can_msg_set(msg, "DAS_hazardLightRequest", 1)
  can_msg_send(msg)
  print("Hazards ON")

  timer_after(3000, def()
    var m2 = can_msg_get("DAS_bodyControls", 0)
    if m2 != nil
      can_msg_set(m2, "DAS_hazardLightRequest", 0)
      can_msg_send(m2)
      print("Hazards OFF")
    end
  end)
end

def setup()
  timer_every(250, poll)
  action_register("hazards", /-> hazards_on())
  print("Tesla gear LED demo loaded")
end

def teardown()
  led_off()
  print("Tesla gear LED demo stopped")
end
