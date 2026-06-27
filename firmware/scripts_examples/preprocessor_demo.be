# @name Preprocessor demo
# @description Exercises every preprocessor pattern for visual inspection.
# @bus 0

# ---- 1. can_msg_get ----
# Bus-first (rewritten)
var a = can_msg_get(0, "DAS_bodyControls")
var b = can_msg_get(1, "VCLEFT_doorStatus")

# Name-first (passes through unchanged)
var c = can_msg_get("DAS_bodyControls", 0)

# Already numeric (passes through)
var d = can_msg_get(0, 0x3e9)

# ---- 2. can_msg_new ----
# With optional bus arg (rewritten)
var e = can_msg_new("UI_vehicleControl3", 0)
var f = can_msg_new("DAS_bodyControls")

# Already numeric (passes through)
var g = can_msg_new(0x294)

# ---- 3. msg_sig_get ----
# Extracts a signal from a pre-read draft (inlined to draft["data"].getbits)
var fl_msg = can_msg_get(0, "VCLEFT_doorStatus")
var h = 0
if fl_msg != nil h = msg_sig_get(fl_msg, "VCLEFT_frontLatchStatus") end

# ---- 4. msg_sig_set ----
# Scale=1, offset=0 (passes value directly)
msg_sig_set(a, "DAS_hazardLightRequest", 1)
msg_sig_set(b, "VCLEFT_frontLatchStatus", 2)

# Scale != 1 (value is scaled: (val - offset) / scale)
msg_sig_set(e, "UI_windowPercentageRequest", 100)

# Bit select signals
msg_sig_set(e, "UI_windowRequestedFL", 1)
msg_sig_set(e, "UI_windowRequestedFR", 0)
msg_sig_set(e, "UI_windowRequest", 4)

# Using an expression as value
var val = 2
msg_sig_set(e, "UI_windowRequestedRL", val && 1)

# ---- 5. can_msg_send ----
can_msg_send(0, a)
can_msg_send(1, e)

# ---- 6. Error cases (emits raise) ----
# Unknown message
var bad = can_msg_get(0, "UNKNOWN_MESSAGE")
var bad2 = can_msg_new("UNKNOWN_MESSAGE")

# Unknown signal
var bad3 = msg_sig_get(a, "UNKNOWN_SIG")
msg_sig_set(a, "UNKNOWN_SIG", 0)

# ---- 7. Typical edge: read then extract ----
var di_msg = can_msg_get(0, "DI_systemStatus")
var sig = 0
if di_msg != nil sig = msg_sig_get(di_msg, "DI_gear") end
if sig > 0
  var tmp = can_msg_new("UI_vehicleControl3")
  msg_sig_set(tmp, "UI_windowRequest", 4)
  can_msg_send(0, tmp)
end
