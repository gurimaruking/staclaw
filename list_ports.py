import serial.tools.list_ports
ports = list(serial.tools.list_ports.comports())
if not ports:
    print("No COM ports found!")
else:
    for p in ports:
        print(f"{p.device}: {p.description}")
