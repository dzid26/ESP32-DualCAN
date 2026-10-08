# @name Pedal stopping mode
# @description Brake rising -> HOLD, gas rising -> CREEP, gas falling (no brake) -> ROLL. Mirror-modify UI_powertrainControl.
# @bus 0
#
# Requires: Tesla Model 3/Y DBC loaded on bus 0.
#
# RX (bus 0):
#   DI_brakePedalState (DI_systemStatus, ID 280): 0=OFF, 1=ON
#   DI_accelPedalPressed (DI_speed, ID 599): 0/1
# TX (bus 0):
#   UI_stoppingMode (UI_powertrainControl, ID 820): 0=STANDARD/ROLL, 1=CREEP, 2=HOLD
#   UI_powertrainControlCounter 52|4, UI_powertrainControlChecksum 56|8 (byte 7)
#
# Safety: never sends a zeroed frame. Skips TX until a real
# UI_powertrainControl mirror has been seen. Only other bits ride along.

var MODE_ROLL = 0
var MODE_CREEP = 1
var MODE_HOLD = 2

var POLL_MS = 50

var prev_brake = 0
var prev_gas = 0

def tesla_checksum(addr, csum_idx, payload)
  var s = (addr & 0xFF) + ((addr >> 8) & 0xFF)
  for i : 0..payload.size() - 1
    if i != csum_idx
      s += payload[i]
    end
  end
  return s & 0xFF
end

# Signal as 0/1; anything else (nil, INVALID=2, SNA) counts as 0.
def read_flag(msg, name)
  var v = msg_sig_get(msg, name)
  if v == nil
    return 0
  end
  return int(v) == 1 ? 1 : 0
end

def request_mode(mode, label)
  var msg = can_msg_get(0, "UI_powertrainControl")
  if msg == nil
    print("stopping-mode: no mirror yet, skip " .. label)
    return
  end
  msg_sig_set(msg, "UI_stoppingMode", mode)
  var c = int(msg_sig_get(msg, "UI_powertrainControlCounter"))
  c = (c + 1) % 16
  msg_sig_set(msg, "UI_powertrainControlCounter", c)
  msg_sig_set(msg, "UI_powertrainControlChecksum", 0)
  var cs = tesla_checksum(msg["id"], 7, msg["data"])
  msg_sig_set(msg, "UI_powertrainControlChecksum", cs)
  can_msg_send(0, msg)
  print("stopping-mode: request " .. label)
end

def poll()
  var sys = can_msg_get(0, "DI_systemStatus")
  var spd = can_msg_get(0, "DI_speed")
  if sys == nil || spd == nil
    return
  end

  var brake = read_flag(sys, "DI_brakePedalState")
  var gas = read_flag(spd, "DI_accelPedalPressed")

  var brake_rising = (brake == 1 && prev_brake != 1)
  var gas_rising = (gas == 1 && prev_gas != 1)
  var gas_falling = (gas == 0 && prev_gas == 1)

  if brake_rising
    request_mode(MODE_HOLD, "HOLD")
  elif gas_rising
    request_mode(MODE_CREEP, "CREEP")
  elif gas_falling && brake != 1
    request_mode(MODE_ROLL, "ROLL")
  end

  prev_brake = brake
  prev_gas = gas
end

def setup()
  # Seed previous state so an already-pressed pedal doesn't count as an edge
  var sys = can_msg_get(0, "DI_systemStatus")
  if sys != nil
    prev_brake = read_flag(sys, "DI_brakePedalState")
  end
  var spd = can_msg_get(0, "DI_speed")
  if spd != nil
    prev_gas = read_flag(spd, "DI_accelPedalPressed")
  end
  timer_every(POLL_MS, poll)
  print("Pedal stopping mode loaded")
end
