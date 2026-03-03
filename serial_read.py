import serial
import time

s = serial.Serial('COM4', 115200, timeout=1)
# Reset the ESP32
s.dtr = False
s.rts = True
time.sleep(0.1)
s.rts = False
time.sleep(5)

data = b''
while True:
    chunk = s.read(s.in_waiting or 1)
    if not chunk:
        break
    data += chunk

s.close()
text = data.decode('utf-8', 'ignore')
import sys
sys.stdout.buffer.write(text.encode('utf-8'))
sys.stdout.buffer.write(b'\n')
