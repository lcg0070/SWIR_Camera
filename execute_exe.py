#multi-process
import subprocess
import mmap
import struct
import time
import win32event
import win32con
import win32api

# visualize
import numpy as np
import cv2

WIDTH = 1280
HEIGHT = 1024

def main():

    # execute Grab_GigE_Release.exe
    app_path = r"Grab_GigE_Release.exe"
    process = subprocess.Popen(app_path)
    print("Waiting for the application to initialize...")
    time.sleep(20)

    # shared memory, mutex, event name
    shared_mem_size = 10 * 1024 * 1024
    shared_mem_name = "Global\\MySharedImageMapping"
    mutex_name = "Global\\MySharedImageMutex"
    event_name = "Global\\MyDataReadyEvent"

    try:
        mm = mmap.mmap(-1, shared_mem_size, tagname=shared_mem_name, access=mmap.ACCESS_READ)
    except Exception as e:
        print("Failed to open memory map:", e)
        return

    try:
        hMutex = win32event.OpenMutex(win32con.SYNCHRONIZE, False, mutex_name)
    except Exception as e:
        print("Failed to open mutex:", e)
        mm.close()
        return

    try:
        hEvent = win32event.OpenEvent(win32con.EVENT_MODIFY_STATE | win32con.SYNCHRONIZE, False, event_name)
    except Exception as e:
        print("Failed to open event:", e)
        win32api.CloseHandle(hMutex)
        mm.close()
        return

    print("Python application started. Waiting for image data...")

    print("Press ESC to exit application...")
    capture_count = 0  # 캡쳐 파일 번호 저장용

    while True:
        # ready for event signal : max 5s
        ret = win32event.WaitForSingleObject(hEvent, 5000)
        if ret == win32con.WAIT_OBJECT_0:
            wait_ret = win32event.WaitForSingleObject(hMutex, 5000)
            if wait_ret == win32con.WAIT_OBJECT_0:
                try:
                    mm.seek(0)
                    # read image size by first four byte
                    data = mm.read(4)
                    if len(data) < 4:
                        print("Insufficient data for image size")
                        continue
                    image_size = struct.unpack("I", data)[0]
                    image_data = mm.read(image_size)

                    image_array = np.frombuffer(image_data, dtype=np.uint8)
                    image = image_array.reshape((HEIGHT, WIDTH))
                    image_resized = cv2.resize(image, dsize=(640, 512), interpolation=cv2.INTER_AREA)

                    cv2.imshow('Image', image_resized)
                    key = cv2.waitKey(1) & 0xFF  # 여기서 키 입력 받음
                    if key == 27:  # ESC
                        break
                    elif key == 32:  # Space
                        filename = f"capture_{capture_count}.png"
                        cv2.imwrite(filename, image)
                        print(f"Captured image saved as {filename}")
                        capture_count += 1
                finally:
                    win32event.ReleaseMutex(hMutex)
            else:
                print("Failed to acquire mutex for reading.")
        else:
            print("No new data received within timeout period.")

    mm.close()
    win32api.CloseHandle(hMutex)
    win32api.CloseHandle(hEvent)

    process.terminate()
    print("Application terminated.")

if __name__ == '__main__':
    main()
