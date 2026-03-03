import serial
import sys

port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
baud = 115200

s = serial.Serial(port, baud, timeout=1)
print(f"Monitoring {port} at {baud}...")
try:
    while True:
        line = s.readline()
        if line:
            print(line.decode("utf-8", "ignore").strip())
except KeyboardInterrupt:
    pass
finally:
    s.close()
