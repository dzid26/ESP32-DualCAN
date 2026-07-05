# @name Bench test
# @description Sends periodic CAN frames for testing without a car.
#              Watch serial monitor (pio device monitor) to see TX/RX.
# @bus 0

var tick = 0

var ids = [0x100, 0x103, 0x118]

def tick_fn()
  # Send three different message IDs so we can see them in the trace
  var b = bytes()
  b.add(tick & 0xFF)
  b.add((tick >> 8) & 0xFF)

  can_send_raw(0, 0x100, b)
  can_send_raw(0, 0x103, b)
  can_send_raw(0, 0x118, b)

  tick += 1

  # Check for looped-back frames on each bus
  for bus : [0, 1]
    for id : ids
      var payload = can_recv_raw(bus, id)
      if payload != nil
        print("bus" .. str(bus) .. " rx: 0x" .. format("%03X", id))
      end
    end
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
