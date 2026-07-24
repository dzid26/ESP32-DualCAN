# @name Easy entry window drop
# @description Easy-entry: lowers windows when exterior door handles are pulled (all 4), restores them when door closes.
# @bus 0
#
# Requires: Tesla Model 3/Y vehicle CAN DBC loaded on bus 0.
#
# Polls VCLEFT_doorStatus / VCRIGHT_doorStatus and reads front/rear
# doorClosureStatus and handlePulled bits. Sends GOTO_PERCENT_RELATIVE commands on
# UI_vehicleControl3 (0x294/660) to drop the corresponding window(s).
#
# UI_windowRequest values:
#   0=IDLE, 1=GOTO_PERCENT, 2=GOTO_VENT, 3=GOTO_CLOSED,
#   4=GOTO_OPEN, 5=GOTO_CLOSED_CARWASH, 6=GOTO_PERCENT_RELATIVE
# UI_windowRequestedFL/FR/RL/RR: 1-bit per-window select.

var DOOR_CLOSED = 2
var DOOR_OPENED = 1

var prev_handle_fl = 0, prev_handle_fr = 0, prev_handle_rl = 0, prev_handle_rr = 0
var prev_door_fl = 0, prev_door_fr = 0, prev_door_rl = 0, prev_door_rr = 0
var was_window_closed_fl = true, was_window_closed_fr = true, was_window_closed_rl = true, was_window_closed_rr = true
var was_auto_dropped_fl = false, was_auto_dropped_fr = false, was_auto_dropped_rl = false, was_auto_dropped_rr = false

def window_closed(st)
  # DBC window position: 0=UNKNOWN 1=CLOSED 2=CRACKED 3=VENT 4=PARTIAL_OPEN 5=FULL_OPEN 6=CLOSED_TRIM_CLEAR
  # Consider window "closed" (in normal at-rest position) if 1-4
  if st == nil return false end
  return st >= 1 && st <= 4
end

def send_window_drop(m)
  print("handle pulled, window dropping")
  msg_sig_set(m, "UI_windowPercentageRequest", 100)
  msg_sig_set(m, "UI_windowRequest", 1)       # GOTO_PERCENT
  can_msg_send(0, m)
end

def send_window_stop(m)
  print("handle released, window stop")
  msg_sig_set(m, "UI_windowPercentageRequest", 0)
  msg_sig_set(m, "UI_windowRequest", 6)        # GOTO_PERCENT_RELATIVE
  can_msg_send(0, m)
end

def send_window_close(m)
  print("door closed, window restoring")
  msg_sig_set(m, "UI_windowRequest", 3)        # GOTO_CLOSED
  can_msg_send(0, m)
end

def poll()
  var m = can_msg_new("UI_vehicleControl3")

  # --- Left side: handles + door state + window state ---
  var msg_l = can_msg_get(0, "VCLEFT_doorStatus")
  var msg_wl  = can_msg_get(0, "VCLEFT_windowStatus")

  if msg_l != nil
    var door_fl = msg_sig_get(msg_l, "VCLEFT_doorClosureStatusFront")
    var door_rl = msg_sig_get(msg_l, "VCLEFT_doorClosureStatusRear")

    # --- FL corner ---
    var mux_wl = 0
    if msg_wl != nil
      mux_wl = msg_sig_get(msg_wl, "VCLEFT_windowStatusIndex")
    end
    if door_fl == DOOR_CLOSED
      if prev_door_fl != DOOR_CLOSED && was_auto_dropped_fl == true && was_window_closed_fl == true
        msg_sig_set(m, "UI_windowRequestedFL", 1)
        send_window_close(m)
      end
      was_auto_dropped_fl = false
      if msg_wl != nil                                     # capture window state before door opens next time
        if mux_wl == 1
          var fl_pos = msg_sig_get(msg_wl, "VCLEFT_windowPositionStateLF")
          was_window_closed_fl = window_closed(fl_pos)
        end
      end
    elif door_fl == DOOR_OPENED
      var handle_fl = msg_sig_get(msg_l, "VCLEFT_frontHandlePulled")
      if handle_fl > 0 && prev_handle_fl == 0 && prev_door_fl == DOOR_OPENED   # second pull while door already open
        print("FL handle pulled, easy-entry drop")
        msg_sig_set(m, "UI_windowRequestedFL", 1)
        send_window_drop(m)
        was_auto_dropped_fl = true
      end
      if handle_fl == 0 && prev_handle_fl == 1
        print("FL handle released")
        msg_sig_set(m, "UI_windowRequestedFL", 1)
        send_window_stop(m)
      end
      prev_handle_fl = handle_fl
    end
    prev_door_fl = door_fl

    # --- RL corner ---
    if door_rl == DOOR_CLOSED
      if prev_door_rl != DOOR_CLOSED && was_auto_dropped_rl == true && was_window_closed_rl == true
        msg_sig_set(m, "UI_windowRequestedRL", 1)
        send_window_close(m)
      end
      was_auto_dropped_rl = false
      if msg_wl != nil                                     # capture window state before door opens next time
        if mux_wl == 1
          var rl_pos = msg_sig_get(msg_wl, "VCLEFT_windowPositionStateLR")
          was_window_closed_rl = window_closed(rl_pos)
        end
      end
    elif door_rl == DOOR_OPENED
      var handle_rl = msg_sig_get(msg_l, "VCLEFT_rearHandlePulled")
      if handle_rl > 0 && prev_handle_rl == 0 && prev_door_rl == DOOR_OPENED   # second pull while door already open
        print("RL handle pulled, easy-entry drop")
        msg_sig_set(m, "UI_windowRequestedRL", 1)
        send_window_drop(m)
        was_auto_dropped_rl = true
      end
      if handle_rl == 0 && prev_handle_rl == 1
        print("RL handle released")
        msg_sig_set(m, "UI_windowRequestedRL", 1)
        send_window_stop(m)
      end
      prev_handle_rl = handle_rl
    end
    prev_door_rl = door_rl
  end

  # --- Right side: handles + door state + window state ---
  var msg_r = can_msg_get(0, "VCRIGHT_doorStatus")
  var msg_wr  = can_msg_get(0, "VCRIGHT_windowStatus")

  if msg_r != nil
    var door_fr = msg_sig_get(msg_r, "VCRIGHT_doorClosureStatusFront")
    var door_rr = msg_sig_get(msg_r, "VCRIGHT_doorClosureStatusRear")

    # --- FR corner ---
    var mux_wr = 0
    if msg_wr != nil
      mux_wr = msg_sig_get(msg_wr, "VCRIGHT_windowStatusIndex")
    end
    if door_fr == DOOR_CLOSED
      if prev_door_fr != DOOR_CLOSED && was_auto_dropped_fr == true && was_window_closed_fr == true
        print("FR door closed, restoring window")
        msg_sig_set(m, "UI_windowRequestedFR", 1)
        send_window_close(m)
      end
      was_auto_dropped_fr = false
      if msg_wr != nil                                     # capture window state before door opens next time
        if mux_wr == 1
          var fr_pos = msg_sig_get(msg_wr, "VCRIGHT_windowPositionStateRF")
          was_window_closed_fr = window_closed(fr_pos)
        end
      end
    elif door_fr == DOOR_OPENED
      var handle_fr = msg_sig_get(msg_r, "VCRIGHT_frontHandlePulled")
      if handle_fr > 0 && prev_handle_fr == 0 && prev_door_fr == DOOR_OPENED   # second pull while door already open
        print("FR handle pulled, easy-entry drop")
        msg_sig_set(m, "UI_windowRequestedFR", 1)
        send_window_drop(m)
        was_auto_dropped_fr = true
      end
      if handle_fr == 0 && prev_handle_fr == 1
        print("FR handle released")
        msg_sig_set(m, "UI_windowRequestedFR", 1)
        send_window_stop(m)
      end
      prev_handle_fr = handle_fr
    end
    prev_door_fr = door_fr

    # --- RR corner ---
    if door_rr == DOOR_CLOSED
      if prev_door_rr != DOOR_CLOSED && was_auto_dropped_rr == true && was_window_closed_rr == true
        print("RR door closed, restoring window")
        msg_sig_set(m, "UI_windowRequestedRR", 1)
        send_window_close(m)
      end
      was_auto_dropped_rr = false
      if msg_wr != nil                                     # capture window state before door opens next time
        if mux_wr == 1
          var rr_pos = msg_sig_get(msg_wr, "VCRIGHT_windowPositionStateRR")
          was_window_closed_rr = window_closed(rr_pos)
        end
      end
    elif door_rr == DOOR_OPENED
      var handle_rr = msg_sig_get(msg_r, "VCRIGHT_rearHandlePulled")
      if handle_rr > 0 && prev_handle_rr == 0 && prev_door_rr == DOOR_OPENED   # second pull while door already open
        print("RR handle pulled, easy-entry drop")
        msg_sig_set(m, "UI_windowRequestedRR", 1)
        send_window_drop(m)
        was_auto_dropped_rr = true
      end
      if handle_rr == 0 && prev_handle_rr == 1
        print("RR handle released")
        msg_sig_set(m, "UI_windowRequestedRR", 1)
        send_window_stop(m)
      end
      prev_handle_rr = handle_rr
    end
    prev_door_rr = door_rr
  end
end

def setup()
  timer_every(150, poll)
end
