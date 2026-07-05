# @name Tiles demo
# @description Registers a few named actions that show up as tiles on the
#              Dashboard. Each action just blinks the onboard LED a different
#              colour so you can verify the tile -> firmware -> Berry path
#              without needing a CAN bus connected.

def flash(r, g, b, dur_ms)
  led_set(r, g, b)
  timer_after(dur_ms, /-> led_off())
end

def setup()
  action_register("blip_red",   /-> flash(40, 0,  0,  150))
  action_register("blip_green", /-> flash(0,  40, 0,  150))
  action_register("blip_blue",  /-> flash(0,  0,  40, 150))
  action_register("rainbow", def ()
    flash(40, 0, 0, 200)
    timer_after(220, /-> flash(0, 40, 0, 200))
    timer_after(440, /-> flash(0, 0, 40, 200))
  end)
end
