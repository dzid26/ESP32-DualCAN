# @name Berry timer ownership probe
# @description Registers a repeating timer from inside a timer callback and from
#              inside an action. Both must stop when the script is disabled.
# @bus 0
#
# Manual check: enable, wait for the first beat, then disable or resave. Beats
# must stop. On broken ownership each timer keeps firing, and repeated cycles
# eventually fill the 16 timer slots until reboot.

var beats = 0

def inner_tick()
  beats += 1
  if beats <= 3 || beats % 10 == 0
    print("inner beat " .. str(beats))
  end
end

def arm_inner()
  var h = timer_every(500, inner_tick)
  print("armed inner timer handle=" .. str(h))
end

def arm_from_action()
  var h = timer_every(500, inner_tick)
  print("armed from action, handle=" .. str(h))
end

def setup()
  action_register("timer probe: arm", /-> arm_from_action())
  timer_after(1000, arm_inner)
  print("timer probe ready")
end

def teardown()
  print("timer probe teardown, beats=" .. str(beats))
end
