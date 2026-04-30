# @name Hello log
# @description Demonstrates the Dashboard log panel and action tiles.
#              Prints a heartbeat every 5 seconds, and registers a 'greet'
#              action that prints + blinks the LED. No CAN traffic required.

var beats = 0

def greet()
  print("hello from berry, beat=" .. str(beats))
  led_set(0, 0, 32)
  timer_after(200, /-> led_off())
end

def heartbeat()
  beats += 1
  print("heartbeat " .. str(beats))
end

def setup()
  print("hello_log: setup")
  action_register("greet", greet)
  timer_every(5000, heartbeat)
end

def teardown()
  print("hello_log: teardown after " .. str(beats) .. " beats")
end
