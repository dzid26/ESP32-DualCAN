# @name Loopback LED
# @description TX a counter on CAN0, light green when CAN1 receives it (loopback
#              wiring), red on miss. Also bridges any other CAN0 RX to CAN1.
# @bus 0,1

var tick = 0

def tick_fn()
  # TX a counter frame on CAN0
  var b = bytes()
  b.add(tick & 0xFF)
  b.add((tick >> 8) & 0xFF)
  b.add((tick >> 16) & 0xFF)
  b.add((tick >> 24) & 0xFF)
  can_send(0, 0x123, b)
  tick += 1

  # Check CAN1 for loopback
  var rx = can_receive(1)
  if rx != nil
    led_set(0, 32, 0)   # green
  else
    led_set(32, 0, 0)   # red
  end

  # Bridge: forward any CAN0 rx to CAN1
  rx = can_receive(0)
  while rx != nil
    can_send(1, rx.item(0), rx.item(1))
    rx = can_receive(0)
  end
end

def setup()
  timer_every(50, tick_fn)
end
