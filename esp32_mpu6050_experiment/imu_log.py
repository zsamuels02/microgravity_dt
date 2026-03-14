import requests
import csv
import time
from datetime import datetime

url = "http://192.168.4.1/data"
filename = "imu_log_heavyt1.csv"

print(f"Logging to {filename} — press Ctrl+C to stop")

with open(filename, "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["timestamp", "ax", "ay", "az", "gx", "gy", "gz"])
    
    while True:
        try:
            r = requests.get(url, timeout=2).json()
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")
            row = [timestamp, r["ax"], r["ay"], r["az"], r["gx"], r["gy"], r["gz"]]
            writer.writerow(row)
            f.flush()
            print(row)  # so you can see it's working
            time.sleep(0.1)

        except requests.exceptions.RequestException as e:
            print(f"Connection error: {e} — retrying...")
            time.sleep(1)