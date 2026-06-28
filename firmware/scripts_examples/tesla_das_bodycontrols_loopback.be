# @name Tesla DAS_bodyControls round-trip test
# @description Builds a DAS_bodyControls message via msg_sig_set, verifies
#              every signal with msg_sig_get, then sends and reads back via
#              loopback to verify can_msg_send wire output.
# @bus 0,1
#
# Requires: Tesla Model 3/Y Vehicle DBC loaded on bus 0. Bench setup must
# have CAN0 and CAN1 transceivers wired together (TX0→RX1, TX1→RX0).
#
# DAS_bodyControls (ID 0x3E9 / 1001):
#   DAS_headlightRequest       0|2  Intel + (1,0)
#   DAS_hazardLightRequest     2|2  Intel + (1,0)
#   DAS_wiperSpeed             4|4  Intel + (1,0)
#   DAS_bodyControlsCounter   52|4  Intel + (1,0)
#   DAS_bodyControlsChecksum  56|8  Intel + (1,0)

def check(label, got, expect)
  if got == expect
    print("  PASS " .. label)
    return 0
  end
  print("  FAIL " .. label .. ": got " .. str(got) .. " expected " .. str(expect))
  return 1
end

def run_test()
  var errors = 0
  print("=== DAS_bodyControls round-trip test ===\n")

  # ---- Phase 1: Build ----
  print("1: Build message")
  var msg = can_msg_new("DAS_bodyControls", 0)

  msg_sig_set(msg, "DAS_headlightRequest", 2)
  msg_sig_set(msg, "DAS_hazardLightRequest", 1)
  msg_sig_set(msg, "DAS_wiperSpeed", 9)
  msg_sig_set(msg, "DAS_bodyControlsCounter", 5)
  print("   encoded: " .. str(msg["data"]))

  # ---- Phase 2: Verify ----
  print("\n2: Verify every signal")
  var v = msg_sig_get(msg, 0, 2, false, false, 1, 0)
  errors += check("headlightRequest", v, 2)
  v = msg_sig_get(msg, 2, 2, false, false, 1, 0)
  errors += check("hazardLightRequest", v, 1)
  v = msg_sig_get(msg, 4, 4, false, false, 1, 0)
  errors += check("wiperSpeed", v, 9)
  v = msg_sig_get(msg, 52, 4, false, false, 1, 0)
  errors += check("counter", v, 5)

  # ---- Phase 3: Send and verify loopback ----
  print("\n3: Send on bus 0, verify loopback on bus 1")
  var tx_id = msg["id"]
  var tx_data = msg["data"]
  print("   tx id=0x" .. format("%03X", tx_id) .. " data=" .. str(tx_data))
  can_msg_send(0, msg)

  var rx = can_recv_raw(1, tx_id, 50)
  if rx == nil
    errors += 1
    print("  FAIL no loopback on bus 1 for id 0x" .. format("%03X", tx_id))
  else
    print("   rx data=" .. str(rx))
    errors += check("loopback data matches", rx, tx_data)
  end

  # ---- Phase 4: Modify ----
  print("\n4: Modify signals")
  msg_sig_set(msg, "DAS_hazardLightRequest", 0)
  msg_sig_set(msg, "DAS_headlightRequest", 1)
  msg_sig_set(msg, "DAS_wiperSpeed", 3)
  msg_sig_set(msg, "DAS_bodyControlsCounter", 6)
  print("   modified: " .. str(msg["data"]))

  # ---- Phase 5: Re-verify ----
  print("\n5: Re-verify after modification")
  v = msg_sig_get(msg, 0, 2, false, false, 1, 0)
  errors += check("mod headlightRequest", v, 1)
  v = msg_sig_get(msg, 2, 2, false, false, 1, 0)
  errors += check("mod hazardLightRequest", v, 0)
  v = msg_sig_get(msg, 4, 4, false, false, 1, 0)
  errors += check("mod wiperSpeed", v, 3)
  v = msg_sig_get(msg, 52, 4, false, false, 1, 0)
  errors += check("mod counter", v, 6)

  # ---- Phase 6: Re-send and verify loopback ----
  print("\n6: Re-send on bus 0, verify loopback on bus 1")
  var tx_data2 = msg["data"]
  print("   tx id=0x" .. format("%03X", tx_id) .. " data=" .. str(tx_data2))
  can_msg_send(0, msg)

  var rx2 = can_recv_raw(1, tx_id, 50)
  if rx2 == nil
    errors += 1
    print("  FAIL no loopback on bus 1 for second send")
  else
    print("   rx data=" .. str(rx2))
    errors += check("loopback2 data matches", rx2, tx_data2)
  end

  # ---- Summary ----
  print("\n=== " .. str(errors) .. " failure(s) ===")
  if errors == 0
    print("ALL TESTS PASSED")
    led_set(0, 64, 0)
  else
    print("SOME TESTS FAILED")
    led_set(64, 0, 0)
  end
  timer_after(3000, /-> led_set(0, 0, 0))
end

run_test()
