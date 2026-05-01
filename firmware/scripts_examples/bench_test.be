# @name Bench test
# @description Sends periodic CAN frames for testing without a car.
#              Watch serial monitor (pio device monitor) to see TX/RX.
# @bus 0

var tick = 0

def tick_fn()
  # Send three different message IDs so we can see them in the trace
  var b = bytes()
  b.add(tick & 0xFF)
  b.add((tick >> 8) & 0xFF)

  can_send(0, 0x100, b)     # simulated DI_torque
  can_send(0, 0x103, b)     # simulated VCRIGHT_doorStatus
  can_send(0, 0x118, b)     # simulated DI_state

  tick += 1

  # Drain and print any received frames (from loopback or external)
  var rx = can_receive(0)
  while rx != nil
    print("bus0 rx: 0x" .. format("%03X", rx.item(0)))
    rx = can_receive(0)
  end

  rx = can_receive(1)
  while rx != nil
    print("bus1 rx: 0x" .. format("%03X", rx.item(0)))
    rx = can_receive(1)
  end

  # Cycle LED color based on tick
  var phase = tick % 3
  if phase == 0
    led_set(16, 0, 0)    # red
  elif phase == 1
    led_set(0, 16, 0)    # green
  else
    led_set(0, 0, 16)    # blue
  end
end

def setup()
  timer_every(100, tick_fn)
end
