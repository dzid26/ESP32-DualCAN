# @name Tesla DAS_bodyControls round-trip test
# @description Builds a DAS_bodyControls message via can_msg_set, verifies
#              every signal with signal_decode, modifies and re-verifies.
# @bus 0
#
# Requires: Tesla Model 3/Y Vehicle DBC loaded on bus 0.
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

  can_msg_set(msg, "DAS_headlightRequest", 2)
  can_msg_set(msg, "DAS_hazardLightRequest", 1)
  can_msg_set(msg, "DAS_wiperSpeed", 9)
  can_msg_set(msg, "DAS_bodyControlsCounter", 5)
  print("   encoded: " .. str(msg["data"]))

  # ---- Phase 2: Verify ----
  print("\n2: Verify every signal")

  var v = signal_decode(msg["data"], 0, 2, false, false, 1, 0)
  errors += check("headlightRequest", v, 2)

  v = signal_decode(msg["data"], 2, 2, false, false, 1, 0)
  errors += check("hazardLightRequest", v, 1)

  v = signal_decode(msg["data"], 4, 4, false, false, 1, 0)
  errors += check("wiperSpeed", v, 9)

  v = signal_decode(msg["data"], 52, 4, false, false, 1, 0)
  errors += check("counter", v, 5)

  # ---- Phase 3: Send ----
  print("\n3: Send on bus 0")
  can_msg_send(msg)

  # ---- Phase 4: Modify ----
  print("\n4: Modify signals")
  can_msg_set(msg, "DAS_hazardLightRequest", 0)
  can_msg_set(msg, "DAS_headlightRequest", 1)
  can_msg_set(msg, "DAS_wiperSpeed", 3)
  can_msg_set(msg, "DAS_bodyControlsCounter", 6)
  print("   modified: " .. str(msg["data"]))

  # ---- Phase 5: Re-verify ----
  print("\n5: Re-verify after modification")

  v = signal_decode(msg["data"], 0, 2, false, false, 1, 0)
  errors += check("mod headlightRequest", v, 1)

  v = signal_decode(msg["data"], 2, 2, false, false, 1, 0)
  errors += check("mod hazardLightRequest", v, 0)

  v = signal_decode(msg["data"], 4, 4, false, false, 1, 0)
  errors += check("mod wiperSpeed", v, 3)

  v = signal_decode(msg["data"], 52, 4, false, false, 1, 0)
  errors += check("mod counter", v, 6)

  # ---- Phase 6: Re-send ----
  print("\n6: Re-send on bus 0")
  can_msg_send(msg)

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
