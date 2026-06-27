# @name Window drop on handle pull
# @description Lowers the front window when the exterior door handle is pulled.
# @bus 0
#
# Requires: Tesla Model 3/Y vehicle CAN DBC loaded on bus 0.
#
# Polls VCLEFT_doorStatus / VCRIGHT_doorStatus via msg_sig_get and reads
# the frontHandlePulled bit with getbits. Sends a GOTO_OPEN command on
# UI_vehicleControl3 (0x294/660) for the corresponding window.
#
# UI_windowRequest values:
#   0=IDLE, 1=GOTO_PERCENT, 2=GOTO_VENT, 3=GOTO_CLOSED,
#   4=GOTO_OPEN, 5=GOTO_CLOSED_CARWASH
# UI_windowRequestedFL/FR/RL/RR: 1-bit per-window select.

var fl_prev = 0
var fr_prev = 0

def poll()
  var msg = can_msg_new("UI_vehicleControl3")
  var do_send = false

  var msg_fl = can_msg_get(0, "VCLEFT_doorStatus")
  var fl = 0
  if msg_fl != nil fl = msg_sig_get(msg_fl, "VCLEFT_frontHandlePulled") end

  if fl > 0 && fl_prev == 0
    print("FL handle pulled")
    msg_sig_set(msg, "UI_windowRequestedFL", 1)
    do_send = true
  end
  if fl == 0 && fl_prev == 1
    print("FL handle released")
    msg_sig_set(msg, "UI_windowRequestedFL", 1)
    msg_sig_set(msg, "UI_windowRequest", 0)
    can_msg_send(0, msg) # cancel immediately
  end
  fl_prev = fl

  var msg_fr = can_msg_get(0, "VCRIGHT_doorStatus")
  var fr = 0
  if msg_fr != nil fr = msg_sig_get(msg_fr, "VCRIGHT_frontHandlePulled") end

  if fr > 0 && fr_prev == 0
    print("FR handle pulled")
    msg_sig_set(msg, "UI_windowRequestedFR", 1)
    do_send = true
  end
  if fr == 0 && fr_prev == 1
    print("FR handle released")
    msg_sig_set(msg, "UI_windowRequestedFR", 1)
    msg_sig_set(msg, "UI_windowRequest", 0)
    can_msg_send(0, msg) # cancel immediately
  end
  fr_prev = fr

  if do_send
    msg_sig_set(msg, "UI_windowPercentageRequest", 20)
    msg_sig_set(msg, "UI_windowRequest", 1) # OPEN (4) doesn't work
    can_msg_send(0, msg)
  end
end

def setup()
  timer_every(150, poll)
end
