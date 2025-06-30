Import("env")
import time
import serial

def reset_to_bootloader(env, operation="upload"):
    # Get upload port from environment
    port = env.get('UPLOAD_PORT', None)
    if not port:  # If not specified in environment, get from project options
        port = env.GetProjectOption("upload_port")
    
    print(f"Resetting device on {port} for {operation}...")
    
    for attempt in range(3):  # Try up to 3 times
        try:
            # Open serial connection
            ser = serial.Serial(port, 115200)
            
            # First, ensure everything is off
            ser.setDTR(False)
            ser.setRTS(False)
            time.sleep(0.5)  # Longer initial wait
            
            # Force into bootloader mode
            ser.setRTS(True)
            time.sleep(0.5)
            ser.setDTR(True)
            time.sleep(0.5)
            ser.setRTS(False)
            time.sleep(0.5)
            
            # Toggle DTR for good measure
            ser.setDTR(False)
            time.sleep(0.5)
            ser.setDTR(True)
            time.sleep(0.5)
            ser.setDTR(False)
            time.sleep(0.5)
            
            if operation == "uploadfs":
                # Extra reset sequence for SPIFFS
                ser.setRTS(True)
                time.sleep(1.0)
                ser.setRTS(False)
                time.sleep(1.0)
            
            # Final state
            ser.setRTS(True)
            ser.setDTR(False)
            
            # Close the serial connection
            ser.close()
            time.sleep(2.0)  # Long wait after reset
            
            print(f"Reset attempt {attempt + 1} completed")
            return True
            
        except Exception as e:
            print(f"Reset attempt {attempt + 1} failed: {str(e)}")
            try:
                ser.close()
            except:
                pass
            time.sleep(2.0)  # Longer wait between attempts
            
            if attempt == 2:  # Last attempt
                print("All reset attempts failed. Please try:")
                print("1. Unplug the device")
                print("2. Wait 5 seconds")
                print("3. Plug the device back in")
                print("4. Try the upload again")
                return False

def before_upload(source, target, env):
    print("Preparing device for firmware upload...")
    reset_to_bootloader(env, "upload")

def before_uploadfs(source, target, env):
    print("Preparing device for SPIFFS upload...")
    reset_to_bootloader(env, "uploadfs")

def before_erase(source, target, env):
    print("Preparing device for flash erase...")
    reset_to_bootloader(env, "erase")

# Add pre-actions for different operations
env.AddPreAction("upload", before_upload)
env.AddPreAction("uploadfs", before_uploadfs)
env.AddPreAction("erase", before_erase) 