# @name Window vent → full open
# @description Intercepts GOTO_VENT on UI_vehicleControl3 and sends the same
#              message patched to GOTO_PERCENT + 100% instead.
# @bus 0
#
# Requires: Tesla Model 3/Y Vehicle DBC loaded on bus 0.

var prev_req = 0

def poll()
  var msg = can_msg_get(0, "UI_vehicleControl3")
  if msg == nil
    prev_req = 0
    return
  end

  var req = msg_sig_get(msg, "UI_windowRequest")
  if req == 2 && prev_req != 2
    msg_sig_set(msg, "UI_windowPercentageRequest", 100)
    msg_sig_set(msg, "UI_windowRequest", 1)
    print("vent → full open")
    can_msg_send(0, msg)
  end
  prev_req = req
end

def setup()
  timer_every(100, poll)
end
