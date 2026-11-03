# @name Timer owner leak repro
# @description Registers a repeating timer from inside a timer callback and
#              from inside an action. Both are the shapes that used to be
#              tagged script_id 0, so disabling or resaving the script never
#              cancelled them.
# @bus 0
#
# Expected (fixed firmware):
#   enable            -> "armed inner timer" once, then a beat every 500 ms
#   disable or save   -> beats stop, teardown prints, no LEAK lines after
# Buggy firmware:
#   beats keep printing after disable until reboot. Resaving makes the old
#   inner timer survive next to the new one, so the beat rate doubles.
#
# Registry exhaustion variant: drop the outer delay to 100 ms and arm several
# inner timers per fire. MAX_TIMERS is 16 and leaked entries are never freed,
# so a few enable/disable cycles make timer_every() return nil for every
# script until reboot.

var beats = 0

def inner_tick()
  beats += 1
  # Log the first few, then every 10th, so a leak is obvious but the panel
  # does not flood.
  if beats <= 3 || beats % 10 == 0
    print("LEAK inner beat " .. str(beats))
  end
end

def arm_inner()
  # Inside a timer callback: must inherit this script's owner.
  var h = timer_every(500, inner_tick)
  print("armed inner timer handle=" .. str(h))
end

def arm_from_action()
  # Inside an action body: same ownership rule.
  var h = timer_every(500, inner_tick)
  print("armed from action, handle=" .. str(h))
end

def setup()
  action_register("leak: arm from action", /-> arm_from_action())
  timer_after(1000, arm_inner)
  print("leak repro ready — disable or save this file, then watch for beats")
end

def teardown()
  print("teardown after " .. str(beats) .. " beats")
end
