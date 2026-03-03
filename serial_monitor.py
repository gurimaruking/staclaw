import serial
import time
import sys

s = serial.Serial('COM4', 115200, timeout=1)
print("Monitoring COM4... Touch the screen!")
start = time.time()
timeout = 45  # seconds

while time.time() - start < timeout:
    line = s.readline()
    if line:
        text = line.decode('utf-8', 'ignore').strip()
        if text:
            print(text)
            sys.stdout.flush()

s.close()
print("Done.")
