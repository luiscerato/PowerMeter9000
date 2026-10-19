import argparse
from datetime import datetime, timezone
from pathlib import Path
import time

import serial


# Captura el puerto indicado por los argumentos y agrega sus bytes al archivo de salida.
def captureSerial():
    parser = argparse.ArgumentParser(description="Capture serial output without resetting the device")
    parser.add_argument("port")
    parser.add_argument("output", type=Path)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)

    with args.output.open("ab", buffering=0) as logFile:
        while True:
            try:
                with serial.Serial(port=None, baudrate=args.baud, timeout=0.5) as serialPort:
                    serialPort.dtr = False
                    serialPort.rts = False
                    serialPort.port = args.port
                    serialPort.open()
                    writeMarker(logFile, f"{args.port} connected")
                    while True:
                        data = serialPort.read(serialPort.in_waiting or 1)
                        if data:
                            logFile.write(data)
            except (serial.SerialException, OSError) as error:
                writeMarker(logFile, f"{args.port} unavailable: {error}")
                time.sleep(1)


# Agrega una marca con hora UTC del PC; logFile recibe bytes y message describe el estado.
def writeMarker(logFile, message):
    timestamp = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
    logFile.write(f"\r\n[capture {timestamp}] {message}\r\n".encode("utf-8"))


if __name__ == "__main__":
    captureSerial()
