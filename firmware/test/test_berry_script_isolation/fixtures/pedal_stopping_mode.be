# @name Pedal stopping mode
# @description Pedal edges pick CREEP/ROLL; HOLD only comes from driving above
#              25 kph. Each UI_powertrainControl the car sends is answered with
#              the mode patched. Tiles: ROLL / CREEP / HOLD / RELEASE.
# @bus 0
#
# For each 0x334 the car sends, mirror it a few ms later with byte 3 (mode),
# byte 6 (counter+1) and byte 7 (checksum) patched, so ours is the last frame
# the ECU sees. The mode is only patched at or below SPEED_LIMIT.
#
# ESP32 bus 0 (vehicle): 0x334, 0x257 DI_speed, 0x118 DI_systemStatus.
# ESP32 bus 1: 0x145 ESP_status and 0x2B9 DAS_control. (openpilot's src numbers
# for these are 1 and 0/130 respectively - the two numbering schemes are offset.)

var MODE_BIT = 26          # stopping mode = byte 3 bits 2-3
var MODE_ROLL = 0
var MODE_CREEP = 1
var MODE_HOLD = 2
var RELEASE = -1           # release the override

var KPH_TO_MS = 0.2777778

# Patched at or below this, and driving above it is the only way into HOLD. kph.
var SPEED_LIMIT = 25 * KPH_TO_MS

var ACC_ON = 4             # DAS_accState value for ACC_ON
var ACC_DEBOUNCE_MS = 500  # ACC_ON must hold this long before we act on it
var BRAKING = 2            # ESP_driverBrakeApply value for driver braking

var POLL_MS = 10

var want_mode = RELEASE
var last_counter = -1
var prev_gas = false
var prev_brake = false
var prev_fast = false
var cruise = false
var acc_since = 0

def tesla_checksum(addr, csum_idx, payload)
  var s = (addr & 0xFF) + ((addr >> 8) & 0xFF)
  for i : 0..payload.size() - 1
    if i != csum_idx
      s += payload[i]
    end
  end
  return s & 0xFF
end

# DI_vehicleSpeed is kph, 8191 raw = SNA.
def speed_ms(spd)
  var v = msg_sig_get(spd, "DI_vehicleSpeed")
  if v >= 600
    return 0
  end
  return v * KPH_TO_MS
end

# Patch the mode and send, when the mode differs and we are slow enough.
# Records the frame's counter so it is evaluated once.
def try_send(msg)
  last_counter = int(msg_sig_get(msg, "UI_powertrainControlCounter"))
  if want_mode < 0
    return
  end

  if int(msg_sig_get(msg, MODE_BIT, 2, false, false, 1, 0)) == want_mode
    return
  end

  var spd = can_msg_get(0, "DI_speed")
  if spd != nil && speed_ms(spd) >= SPEED_LIMIT
    return
  end

  msg_sig_set(msg, MODE_BIT, 2, false, false, 1, 0, want_mode)
  msg_sig_set(msg, "UI_powertrainControlCounter", (last_counter + 1) % 16)
  msg_sig_set(msg, "UI_powertrainControlChecksum", 0)
  msg_sig_set(msg, "UI_powertrainControlChecksum",
              tesla_checksum(msg["id"], 7, msg["data"]))
  can_msg_send(0, msg)
end

def set_mode(mode, label)
  # Leaving HOLD always passes through ROLL first.
  if mode == MODE_CREEP && want_mode == MODE_HOLD
    mode = MODE_ROLL
    label = "ROLL (exit HOLD)"
  end
  want_mode = mode
  print("stopping-mode: " .. label)
  # Applied by the responder on the next 0x334 from the car: we never transmit
  # outside a car frame, so the counter is always fresh and never repeats.
end

def poll()
  var spd = can_msg_get(0, "DI_speed")
  var v = 0
  if spd != nil
    v = speed_ms(spd)
  end

  # Answer each new car frame.
  var msg = can_msg_get(0, "UI_powertrainControl")
  if msg != nil
    var c = int(msg_sig_get(msg, "UI_powertrainControlCounter"))
    if c != last_counter
      last_counter = c
      try_send(msg)
    end
  end

  var gas = false
  if spd != nil && int(msg_sig_get(spd, "DI_accelPedalPressed")) == 1
    gas = true
  end

  var esp = can_msg_get(1, "ESP_status")
  var brake = false
  if esp != nil && int(msg_sig_get(esp, "ESP_driverBrakeApply")) == BRAKING
    brake = true
  end

  var das = can_msg_get(1, "DAS_control")
  var acc_on = false
  if das != nil && int(msg_sig_get(das, "DAS_accState")) == ACC_ON
    acc_on = true
  end

  # Debounce engagement. A one-frame ACC_ON used to latch HOLD permanently, and
  # a real engagement is never instant anyway. Release is immediate.
  if acc_on
    if acc_since == 0
      acc_since = millis()
    elif !cruise && (millis() - acc_since) >= ACC_DEBOUNCE_MS
      cruise = true
    end
  else
    cruise = false
    acc_since = 0
  end

  var fast = v >= SPEED_LIMIT

  # Only way into HOLD: going above SPEED_LIMIT (so the stop is already HOLD).
  # Disabled for testing.
  # if fast && !prev_fast
  #   set_mode(MODE_HOLD, "HOLD (>25 kph)")
  # end
  # prev_fast = fast

  if cruise
    if want_mode != MODE_HOLD
      set_mode(MODE_HOLD, "HOLD (cruise)")
    end
  elif brake && !prev_brake
    set_mode(MODE_ROLL, "ROLL")
  elif gas && !prev_gas
    set_mode(MODE_CREEP, "CREEP")
  end

  prev_gas = gas
  prev_brake = brake
end

def setup()
  can_msg_get(0, "UI_powertrainControl")
  can_msg_get(0, "DI_speed")
  can_msg_get(1, "DAS_control")
  can_msg_get(1, "ESP_status")

  action_register("stopping: ROLL",    /-> set_mode(MODE_ROLL, "ROLL"))
  action_register("stopping: CREEP",   /-> set_mode(MODE_CREEP, "CREEP"))
  action_register("stopping: HOLD",    /-> set_mode(MODE_HOLD, "HOLD"))
  action_register("stopping: RELEASE", /-> set_mode(RELEASE, "RELEASE"))

  timer_every(POLL_MS, poll)
  print("pedal stopping mode ready")
end
