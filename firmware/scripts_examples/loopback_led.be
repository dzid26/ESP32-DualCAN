# @name Loopback LED
# @description TX a counter on CAN0, light green when CAN1 receives it (loopback
#              wiring), red on miss. Also bridges any other CAN0 RX to CAN1.
# @bus 0,1

var tick = 0

def send_fn()
  # TX a counter frame on CAN0
  var b = bytes()
  b.add(tick & 0xFF)
  b.add((tick >> 8) & 0xFF)
  b.add((tick >> 16) & 0xFF)
  b.add((tick >> 24) & 0xFF)
  can_send_raw(0, 0x123, b)
  tick += 1
end

def recv_fn()
  # Check CAN1 for loopback
  var rx_data = can_recv_raw(1, 0x123)
  if rx_data != nil
    led_set(0, 32, 0)   # green
  else
    led_set(32, 0, 0)   # red
  end
end

def teardown()
  led_set(0, 0, 0)
end

def setup()
  timer_every(100, send_fn)
  timer_every(200, recv_fn)
end
