# @name Window drop on handle pull
# @description Lowers windows when exterior door handles are pulled (all 4).
# @bus 0
#
# Requires: Tesla Model 3/Y vehicle CAN DBC loaded on bus 0.
#
# Polls VCLEFT_doorStatus / VCRIGHT_doorStatus and reads front/rear
# handlePulled bits. Sends GOTO_PERCENT_RELATIVE commands on
# UI_vehicleControl3 (0x294/660) to drop the corresponding window(s).
#
# UI_windowRequest values:
#   0=IDLE, 1=GOTO_PERCENT, 2=GOTO_VENT, 3=GOTO_CLOSED,
#   4=GOTO_OPEN, 5=GOTO_CLOSED_CARWASH, 6=GOTO_PERCENT_RELATIVE
# UI_windowRequestedFL/FR/RL/RR: 1-bit per-window select.

var fl_prev = 0, fr_prev = 0, rl_prev = 0, rr_prev = 0

def send_drop(m)
  msg_sig_set(m, "UI_windowPercentageRequest", 100)
  msg_sig_set(m, "UI_windowRequest", 1)
  can_msg_send(0, m)
end

def send_stop(m)
  msg_sig_set(m, "UI_windowPercentageRequest", 0)
  msg_sig_set(m, "UI_windowRequest", 6)
  can_msg_send(0, m)
end

def poll()
  var m = can_msg_new("UI_vehicleControl3")

  var msg_l = can_msg_get(0, "VCLEFT_doorStatus")
  if msg_l != nil
    var fl = msg_sig_get(msg_l, "VCLEFT_frontHandlePulled")
    var rl = msg_sig_get(msg_l, "VCLEFT_rearHandlePulled")

    if fl > 0 && fl_prev == 0
      print("FL handle pulled")
      msg_sig_set(m, "UI_windowRequestedFL", 1)
      send_drop(m)
    end
    if fl == 0 && fl_prev == 1
      print("FL handle released")
      msg_sig_set(m, "UI_windowRequestedFL", 1)
      send_stop(m)
    end
    fl_prev = fl

    if rl > 0 && rl_prev == 0
      print("RL handle pulled")
      msg_sig_set(m, "UI_windowRequestedRL", 1)
      send_drop(m)
    end
    if rl == 0 && rl_prev == 1
      print("RL handle released")
      msg_sig_set(m, "UI_windowRequestedRL", 1)
      send_stop(m)
    end
    rl_prev = rl
  end

  var msg_r = can_msg_get(0, "VCRIGHT_doorStatus")
  if msg_r != nil
    var fr = msg_sig_get(msg_r, "VCRIGHT_frontHandlePulled")
    var rr = msg_sig_get(msg_r, "VCRIGHT_rearHandlePulled")

    if fr > 0 && fr_prev == 0
      print("FR handle pulled")
      msg_sig_set(m, "UI_windowRequestedFR", 1)
      send_drop(m)
    end
    if fr == 0 && fr_prev == 1
      print("FR handle released")
      msg_sig_set(m, "UI_windowRequestedFR", 1)
      send_stop(m)
    end
    fr_prev = fr

    if rr > 0 && rr_prev == 0
      print("RR handle pulled")
      msg_sig_set(m, "UI_windowRequestedRR", 1)
      send_drop(m)
    end
    if rr == 0 && rr_prev == 1
      print("RR handle released")
      msg_sig_set(m, "UI_windowRequestedRR", 1)
      send_stop(m)
    end
    rr_prev = rr
  end
end

def setup()
  timer_every(150, poll)
end
