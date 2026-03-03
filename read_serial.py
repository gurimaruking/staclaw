import serial, time, sys
s = serial.Serial('COM4', 115200, timeout=1)
# Reset device via DTR toggle
s.dtr = False
time.sleep(0.1)
s.dtr = True
time.sleep(0.5)
s.reset_input_buffer()
# Read output for 8 seconds
end = time.time() + 8
while time.time() < end:
    line = s.readline()
    if line:
        print(line.decode('utf-8', 'replace'), end='')
s.close()
