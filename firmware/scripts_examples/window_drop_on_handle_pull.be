# @name Easy-entry window drop on handle pull
# @description Easy-entry: lowers windows when exterior door handles are pulled (all 4), restores them when door closes.
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

var DOOR_CLOSED = 1
var DOOR_OPEN = 4

var prev_handle_fl = 0, prev_handle_fr = 0, prev_handle_rl = 0, prev_handle_rr = 0
var prev_door_fl = 0, prev_door_fr = 0, prev_door_rl = 0, prev_door_rr = 0
var was_window_closed_fl = false, was_window_closed_fr = false, was_window_closed_rl = false, was_window_closed_rr = false
var was_auto_dropped_fl = false, was_auto_dropped_fr = false, was_auto_dropped_rl = false, was_auto_dropped_rr = false

def window_closed(st)
  # DBC window position: 0=UNKNOWN 1=CLOSED 2=CRACKED 3=VENT 4=PARTIAL_OPEN 5=FULL_OPEN 6=CLOSED_TRIM_CLEAR
  # Consider window "closed" (in normal at-rest position) if 1-4
  if st == nil return false end
  return st >= 1 && st <= 4
end

def send_window_drop(m)
  msg_sig_set(m, "UI_windowPercentageRequest", 100)
  msg_sig_set(m, "UI_windowRequest", 1)       # GOTO_PERCENT
  can_msg_send(0, m)
end

def send_window_stop(m)
  msg_sig_set(m, "UI_windowPercentageRequest", 0)
  msg_sig_set(m, "UI_windowRequest", 6)        # GOTO_PERCENT_RELATIVE
  can_msg_send(0, m)
end

def send_window_close(m)
  msg_sig_set(m, "UI_windowRequest", 3)        # GOTO_CLOSED
  can_msg_send(0, m)
end

def poll()
  var m = can_msg_new("UI_vehicleControl3")

  # --- Left side: handles + door state + window state ---

  var msg_dl2 = can_msg_get(0, "VCLEFT_doorStatus2")   # door state (open/closed etc.)
  var msg_dl  = can_msg_get(0, "VCLEFT_doorStatus")    # handle state
  var msg_wl  = can_msg_get(0, "VCLEFT_windowStatus")  # window state

  if msg_dl2 != nil
    var door_fl = msg_sig_get(msg_dl2, "VCLEFT_frontDoorState")
    var door_rl = msg_sig_get(msg_dl2, "VCLEFT_rearDoorState")

    # --- FL corner ---
    if door_fl == DOOR_CLOSED
      if msg_wl != nil                                     # capture window state before door opens next time
        var fl_pos = msg_sig_get(msg_wl, "VCLEFT_windowPositionStateLF")
        was_window_closed_fl = window_closed(fl_pos)
        was_auto_dropped_fl = false
      end
      if prev_door_fl != DOOR_CLOSED && was_auto_dropped_fl == true && was_window_closed_fl == true  # restore on closed rising edge
        print("FL door closed, restoring window")
        msg_sig_set(m, "UI_windowRequestedFL", 1)
        send_window_close(m)
        was_auto_dropped_fl = false
      end
    elif door_fl == DOOR_OPEN
      if msg_dl != nil
        var handle_fl = msg_sig_get(msg_dl, "VCLEFT_frontHandlePulled")
        if handle_fl > 0 && prev_handle_fl == 0
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
    end
    prev_door_fl = door_fl

    # --- RL corner ---
    if door_rl == DOOR_CLOSED
      if msg_wl != nil                                     # capture window state before door opens next time
        var rl_pos = msg_sig_get(msg_wl, "VCLEFT_windowPositionStateLR")
        was_window_closed_rl = window_closed(rl_pos)
        was_auto_dropped_rl = false
      end
      if prev_door_rl != DOOR_CLOSED && was_auto_dropped_rl == true && was_window_closed_rl == true  # restore on closed rising edge
        print("RL door closed, restoring window")
        msg_sig_set(m, "UI_windowRequestedRL", 1)
        send_window_close(m)
        was_auto_dropped_rl = false
      end
    elif door_rl == DOOR_OPEN
      if msg_dl != nil
        var handle_rl = msg_sig_get(msg_dl, "VCLEFT_rearHandlePulled")
        if handle_rl > 0 && prev_handle_rl == 0
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
    end
    prev_door_rl = door_rl
  end

  # --- Right side: handles + door state + window state ---

  var msg_dr2 = can_msg_get(0, "VCRIGHT_doorStatus2")  # door state (open/closed etc.)
  var msg_dr  = can_msg_get(0, "VCRIGHT_doorStatus")   # handle state
  var msg_wr  = can_msg_get(0, "VCRIGHT_windowStatus") # window state

  if msg_dr2 != nil
    var door_fr = msg_sig_get(msg_dr2, "VCRIGHT_frontDoorState")
    var door_rr = msg_sig_get(msg_dr2, "VCRIGHT_rearDoorState")

    # --- FR corner ---
    if door_fr == DOOR_CLOSED
      if msg_wr != nil                                     # capture window state before door opens next time
        var fr_pos = msg_sig_get(msg_wr, "VCRIGHT_windowPositionStateRF")
        was_window_closed_fr = window_closed(fr_pos)
        was_auto_dropped_fr = false
      end
      if prev_door_fr != DOOR_CLOSED && was_auto_dropped_fr == true && was_window_closed_fr == true  # restore on closed rising edge
        print("FR door closed, restoring window")
        msg_sig_set(m, "UI_windowRequestedFR", 1)
        send_window_close(m)
        was_auto_dropped_fr = false
      end
    elif door_fr == DOOR_OPEN
      if msg_dr != nil
        var handle_fr = msg_sig_get(msg_dr, "VCRIGHT_frontHandlePulled")
        if handle_fr > 0 && prev_handle_fr == 0
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
    end
    prev_door_fr = door_fr

    # --- RR corner ---
    if door_rr == DOOR_CLOSED
      if msg_wr != nil                                     # capture window state before door opens next time
        var rr_pos = msg_sig_get(msg_wr, "VCRIGHT_windowPositionStateRR")
        was_window_closed_rr = window_closed(rr_pos)
        was_auto_dropped_rr = false
      end
      if prev_door_rr != DOOR_CLOSED && was_auto_dropped_rr == true && was_window_closed_rr == true  # restore on closed rising edge
        print("RR door closed, restoring window")
        msg_sig_set(m, "UI_windowRequestedRR", 1)
        send_window_close(m)
        was_auto_dropped_rr = false
      end
    elif door_rr == DOOR_OPEN
      if msg_dr != nil
        var handle_rr = msg_sig_get(msg_dr, "VCRIGHT_rearHandlePulled")
        if handle_rr > 0 && prev_handle_rr == 0
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
    end
    prev_door_rr = door_rr
  end
end

def setup()
  timer_every(150, poll)
end
