import serial
import time
import sys

port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
s = serial.Serial(port, 115200, timeout=2)
print(f"Reading {port}... (30 seconds, press Mic button now)")
end = time.time() + 30
while time.time() < end:
    line = s.readline()
    if line:
        text = line.decode("utf-8", "ignore").strip()
        if text:
            print(text)
s.close()
print("Done.")
