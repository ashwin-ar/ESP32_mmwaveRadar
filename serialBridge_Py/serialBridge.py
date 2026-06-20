import serial
import threading
import sys

# --- CONFIGURATION (Match your COM ports) ---
RADAR_PORT = 'COM4'   # The TI Radar High-Speed Data Port
ESP32_PORT = 'COM6'   # Your ESP32 Programming Port
BAUD_RATE = 921600

print("==================================================")
print("     LAUNCHING DUAL-THREADED SERIAL BRIDGE        ")
print("==================================================")

try:
    # Open connections with explicit timeouts to prevent threads from locking up permanently
    radar = serial.Serial(port=RADAR_PORT, baudrate=BAUD_RATE, timeout=0.5)
    esp32 = serial.Serial(port=ESP32_PORT, baudrate=BAUD_RATE, timeout=0.5)
    
    print(f"[INIT] Radar Port opened: {RADAR_PORT}")
    print(f"[INIT] ESP32 Port opened: {ESP32_PORT}")
    print(f"[INIT] Both channels running at {BAUD_RATE} baud.")
except Exception as e:
    print(f"[CRITICAL ERROR] Failed to open ports: {e}")
    sys.exit(1)

# Flag to signal threads to stop when Ctrl+C is pressed
stop_event = threading.Event()

# --- THREAD 1: RADAR -> ESP32 (Binary Stream Pipeline) ---
def radar_to_esp32_pipeline():
    print("[THREAD RUNNING] Radar to ESP32 stream active.")
    while not stop_event.is_set():
        try:
            if radar.in_waiting > 0:
                # Read all available raw binary bytes from the radar
                raw_bytes = radar.read(radar.in_waiting)
                # Forward them immediately to the ESP32
                esp32.write(raw_bytes)
        except Exception as e:
            print(f"\n[ERROR Thread 1] Data drop/Disconnection: {e}")
            stop_event.set()
            break

# --- THREAD 2: ESP32 -> SERIAL MONITOR (Text Return Pipeline) ---
def esp32_to_monitor_pipeline():
    print("[THREAD RUNNING] ESP32 to Terminal Monitor active.")
    while not stop_event.is_set():
        try:
            if esp32.in_waiting > 0:
                # Read incoming print statements sent back by the ESP32
                # using readline() assuming standard text print formats ending in \n
                incoming_line = esp32.readline()
                if incoming_line:
                    try:
                        # Decode the text bytes to string and print to the PC terminal
                        print(incoming_line.decode('utf-8', errors='ignore'), end='')
                    except Exception:
                        pass
        except Exception as e:
            print(f"\n[ERROR Thread 2] Terminal feed dropped: {e}")
            stop_event.set()
            break

# --- MAIN EXECUTION COORDINATOR ---
if __name__ == '__main__':
    # Create the two worker threads
    t1 = threading.Thread(target=radar_to_esp32_pipeline, daemon=True)
    t2 = threading.Thread(target=esp32_to_monitor_pipeline, daemon=True)
    
    # Start both threads concurrently
    t1.start()
    t2.start()
    
    print("\n---> SYSTEM IS LIVE. Press Ctrl+C in this terminal window to stop <---")
    
    try:
        # Keep main execution alive while threads process data in the background
        while not stop_event.is_set():
            stop_event.wait(timeout=1.0)
            
    except KeyboardInterrupt:
        print("\n[SHUTDOWN] Interrupted by user. Closing threads safely...")
        stop_event.set()
        
    finally:
        # Clean closing sequences for safety
        if radar.is_open: 
            radar.close()
        if esp32.is_open: 
            esp32.close()
        print("[SHUTDOWN] Ports unlocked and closed successfully. Bridge offline.")