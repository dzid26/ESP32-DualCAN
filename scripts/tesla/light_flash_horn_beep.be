# @name Light flash → horn beep
# @description When UI_vehicleControl3 requests a light flash (lock/unlock
#              confirmation, mobile-app find, sentry alarm), also beep the
#              horn briefly via UI_shortHornRequest.
# @bus 0
#
# Requires: Tesla Model 3/Y Vehicle DBC loaded on bus 0.
#
# UI_lightFlashRequest values:
#   0=INACTIVE, 1=HOMELINK, 2=MOBILE_APP, 3=SENTRY

var prev_flash = 0

def poll()
  var msg = can_msg_get(0, "UI_vehicleControl3")
  if msg == nil
    prev_flash = 0
    return
  end

  var flash = msg_sig_get(msg, "UI_lightFlashRequest")
  if flash > 0 && prev_flash == 0
    var override = can_msg_new("UI_vehicleControl3")
    msg_sig_set(override, "UI_shortHornRequest", 1)
    print("light flash (" .. str(flash) .. ") → short horn beep")
    can_msg_send(0, override)

    timer_after(200, def()
      var off = can_msg_new("UI_vehicleControl3")
      msg_sig_set(off, "UI_shortHornRequest", 0)
      print("  horn off")
      can_msg_send(0, off)
    end)
  end
  prev_flash = flash
end

def setup()
  timer_every(100, poll)
end
