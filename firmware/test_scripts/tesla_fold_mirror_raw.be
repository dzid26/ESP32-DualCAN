# @name Tesla Fold Mirrors Test
# @description Toggle mirrors when enabling the sript
# Uses raw bytes
# @bus 0

# ChassisCAN (1), VCLEFT_doorStatus (258), VCLEFT_mirrorFoldState (52|3), "FOLDED" (1), "UNFOLDED" (2)
def mirror_folded()
  found = false
  dat = can_recv_raw(0, 258)
  
  if dat == nil
    print("mirrors state not found")
    return nil
  end
  mirror_state = dat.getbits(52, 3)
  # mirror_state = (dat[2] >> 4) && 0x3
  if (mirror_state == 1)
    print("Mirror folded")
    return true
  elif (mirror_state == 2)
    print("Mirror unfolded")
    return false
  else
    print("Mirror state unknown")
    return nil
  end
end

# VehicleCAN (0), UI_vehicleControl 0x273 (627) DLC=8, UI_mirrorFoldRequest (24|2), "RETRACT" (1)
def fold_mirrors()
  # Create byte array directly using hexstring
  can_send_raw(0, 0x273, bytes('0000000100000000')) 
  print("mirrors folding")
end

# VehicleCAN (0), UI_vehicleControl 0x273 (627) DLC=8, UI_mirrorFoldRequest (24|2), "PRESENT" (2)
def unfold_mirrors()
  # Create an 8-byte payload of zeros
  dat = bytes(-8)
  dat.setbits(24, 2, 2) # pos, len, val
  can_send_raw(0, 627, dat)
  print("mirrors unfolding")
end

def setup()
  # Initial state
  if mirror_folded()
    unfold_mirrors()
  else
    fold_mirrors()
  end
end
