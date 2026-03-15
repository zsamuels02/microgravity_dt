import requests
import csv
import time
import json
from datetime import datetime
import pandas as pd
import numpy as np
from scipy.signal import butter, filtfilt
import matplotlib.pyplot as plt

# ── Parameters ────────────────────────────────────────────────────────────────
url         = "http://192.168.4.1/data"
cutoff_hz   = 2
filter_order = 3

# ── Logging ───────────────────────────────────────────────────────────────────
filename = input("Enter a filename for this recording (without .csv): ").strip() + ".csv"
print(f"Logging to {filename} — press Ctrl+C to stop\n")

with open(filename, "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["timestamp", "ax", "ay", "az", "gx", "gy", "gz"])

    try:
        while True:
            try:
                r = requests.get(url, timeout=2).json()
                timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")
                row = [timestamp, r["ax"], r["ay"], r["az"], r["gx"], r["gy"], r["gz"]]
                writer.writerow(row)
                f.flush()
                print(row)
                time.sleep(0.1)
            except requests.exceptions.RequestException as e:
                print(f"Connection error: {e} — retrying...")
                time.sleep(1)

    except KeyboardInterrupt:
        print(f"\nLogging stopped. {filename} saved.")

# ── Ask to process ────────────────────────────────────────────────────────────
process = input("\nProcess and filter this data now? (y/n): ").strip().lower()
if process != "y":
    print("Done.")
    exit()

# ── Load Data ─────────────────────────────────────────────────────────────────
data = pd.read_csv(filename)
timestamps = pd.to_datetime(data["timestamp"], format="%Y-%m-%d %H:%M:%S.%f")
dt = timestamps.diff().dt.total_seconds().dropna()
fs = 1 / dt.mean()
print(f"\nDetected sample rate: {fs:.1f} Hz")

t = (timestamps - timestamps.iloc[0]).dt.total_seconds().values

# ── Design Filter ─────────────────────────────────────────────────────────────
nyquist = fs / 2
Wn      = cutoff_hz / nyquist
b, a    = butter(filter_order, Wn, btype="low")

def lpf(signal):
    return filtfilt(b, a, signal)

# ── Apply Filter ──────────────────────────────────────────────────────────────
ax_f = lpf(data["ax"]);  ay_f = lpf(data["ay"]);  az_f = lpf(data["az"])
gx_f = lpf(data["gx"]);  gy_f = lpf(data["gy"]);  gz_f = lpf(data["gz"])

# ── Save Filtered Data ────────────────────────────────────────────────────────
output_file = filename.replace(".csv", "_filtered.csv")
filtered = pd.DataFrame({
    "timestamp": data["timestamp"],
    "ax": ax_f, "ay": ay_f, "az": az_f,
    "gx": gx_f, "gy": gy_f, "gz": gz_f
})
filtered.to_csv(output_file, index=False)
print(f"Filtered data saved to {output_file}")

# ── Plot Accelerometer ────────────────────────────────────────────────────────
fig, axes = plt.subplots(3, 1, figsize=(10, 8))
fig.suptitle("Accelerometer", fontsize=14)
for i, (raw, filt, label) in enumerate(zip(
        [data["ax"], data["ay"], data["az"]],
        [ax_f, ay_f, az_f],
        ["Accel X (m/s²)", "Accel Y (m/s²)", "Accel Z (m/s²)"])):
    axes[i].plot(t, raw,  color="lightgray", label="Raw")
    axes[i].plot(t, filt, color="blue", linewidth=1.5, label="Filtered")
    axes[i].set_title(label)
    axes[i].set_xlabel("Time (s)")
    axes[i].set_ylabel("m/s²")
    axes[i].legend()
    axes[i].grid(True)
plt.tight_layout()

# ── Plot Gyroscope ────────────────────────────────────────────────────────────
fig, axes = plt.subplots(3, 1, figsize=(10, 8))
fig.suptitle("Gyroscope", fontsize=14)
for i, (raw, filt, label) in enumerate(zip(
        [data["gx"], data["gy"], data["gz"]],
        [gx_f, gy_f, gz_f],
        ["Gyro X (rps)", "Gyro Y (rps)", "Gyro Z (rps)"])):
    axes[i].plot(t, raw,  color="lightgray", label="Raw")
    axes[i].plot(t, filt, color="red", linewidth=1.5, label="Filtered")
    axes[i].set_title(label)
    axes[i].set_xlabel("Time (s)")
    axes[i].set_ylabel("rps")
    axes[i].legend()
    axes[i].grid(True)
plt.tight_layout()

plt.show()