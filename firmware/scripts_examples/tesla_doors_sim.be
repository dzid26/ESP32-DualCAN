# @name Tesla Doors Sim
# @description Simulates left and right doors opening and closing every 2 seconds.
# @bus 0

var door_open = false

def toggle_doors()
  door_open = !door_open

  var latch_val = door_open ? 1 : 2 # 1=Opened, 2=Closed

  # Create an 8-byte payload of zeros
  var b = bytes()
  for i: 0..7
    b.add(0)
  end

  # Both VCRIGHT_frontLatchStatus and VCLEFT_frontLatchStatus
  # are located at bit 0, length 4 bits.
  # We just set the first byte to the latch_val.
  b.set(0, latch_val)

  # VCLEFT_doorStatus ID is 0x102 (258)
  can_send_raw(0, 0x102, b)
  
  # VCRIGHT_doorStatus ID is 0x103 (259)
  can_send_raw(0, 0x103, b)

  print("Doors are now " .. (door_open ? "OPEN" : "CLOSED"))
end

def setup()
  # Initial state
  toggle_doors()
  # Toggle the doors every 2s
  timer_every(2000, toggle_doors)
  print("Tesla Doors Sim loaded")
end
