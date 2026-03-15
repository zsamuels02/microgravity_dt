import requests
import threading
import time
import csv
import io
from datetime import datetime
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.widgets import Button
import numpy as np

# ── Config ────────────────────────────────────────────────────────────────────
BASE_URL    = "http://192.168.4.1"
cutoff_hz   = 2
filter_order = 3

# ── Shared State ──────────────────────────────────────────────────────────────
t_data                         = []
ax_data, ay_data, az_data      = [], [], []
gx_data, gy_data, gz_data      = [], [], []
logging_active                 = False
lock                           = threading.Lock()

# ── Background Thread: polls ESP32 and stores data ───────────────────────────
def poll_loop():
    global logging_active
    while True:
        if logging_active:
            try:
                r = requests.get(f"{BASE_URL}/data", timeout=2).json()
                ts = time.time()
                with lock:
                    base = t_data[0] if t_data else ts
                    t_data.append(ts - base)
                    ax_data.append(r["ax"]); ay_data.append(r["ay"]); az_data.append(r["az"])
                    gx_data.append(r["gx"]); gy_data.append(r["gy"]); gz_data.append(r["gz"])
            except Exception as e:
                print(f"Poll error: {e}")
        time.sleep(0.05)   # ~20Hz poll rate

poll_thread = threading.Thread(target=poll_loop, daemon=True)
poll_thread.start()

# ── Figure Layout ─────────────────────────────────────────────────────────────
fig = plt.figure(figsize=(12, 9))
fig.patch.set_facecolor("#111111")
fig.suptitle("Drop Tower IMU Monitor", color="#00ffff", fontsize=14)

# 6 subplots + button area at bottom
ax_plots = []
labels   = ["Accel X (m/s²)", "Accel Y (m/s²)", "Accel Z (m/s²)",
            "Gyro X (rps)",   "Gyro Y (rps)",   "Gyro Z (rps)"]
colors   = ["#00ff00", "#00cc00", "#009900",
            "#ff4444", "#cc2222", "#991111"]

for i in range(6):
    ax = fig.add_subplot(6, 1, i + 1)
    ax.set_facecolor("#1a1a1a")
    ax.set_ylabel(labels[i], color="#aaaaaa", fontsize=8)
    ax.tick_params(colors="#aaaaaa", labelsize=7)
    for spine in ax.spines.values():
        spine.set_edgecolor("#333333")
    if i < 5:
        ax.set_xticklabels([])
    else:
        ax.set_xlabel("Time (s)", color="#aaaaaa", fontsize=8)
    ax_plots.append(ax)

fig.subplots_adjust(bottom=0.12, hspace=0.15)

lines = [ax_plots[i].plot([], [], color=colors[i], linewidth=1)[0] for i in range(6)]

# ── Status Text ───────────────────────────────────────────────────────────────
status_text = fig.text(0.5, 0.06, "Status: Idle", ha="center",
                       color="#ffff00", fontsize=10)

# ── Buttons ───────────────────────────────────────────────────────────────────
ax_start = fig.add_axes([0.25, 0.01, 0.15, 0.04])
ax_stop  = fig.add_axes([0.42, 0.01, 0.15, 0.04])
ax_save  = fig.add_axes([0.59, 0.01, 0.15, 0.04])

btn_start = Button(ax_start, "Start", color="#004400", hovercolor="#006600")
btn_stop  = Button(ax_stop,  "Stop",  color="#440000", hovercolor="#660000")
btn_save  = Button(ax_save,  "Stop + Download CSV", color="#000044", hovercolor="#000066")

for btn in [btn_start, btn_stop, btn_save]:
    btn.label.set_color("white")

def on_start(event):
    global logging_active
    try:
        requests.get(f"{BASE_URL}/start", timeout=2)
        with lock:
            t_data.clear()
            ax_data.clear(); ay_data.clear(); az_data.clear()
            gx_data.clear(); gy_data.clear(); gz_data.clear()
        logging_active = True
        status_text.set_text("Status: Logging...")
        status_text.set_color("#00ff00")
    except Exception as e:
        status_text.set_text(f"Error: {e}")
        status_text.set_color("#ff0000")

def on_stop(event):
    global logging_active
    logging_active = False
    try:
        requests.get(f"{BASE_URL}/stop", timeout=2)
        status_text.set_text(f"Status: Stopped. ({len(t_data)} rows)")
        status_text.set_color("#ffff00")
    except Exception as e:
        status_text.set_text(f"Error: {e}")
        status_text.set_color("#ff0000")

def on_save(event):
    on_stop(event)
    try:
        # Download CSV from ESP32
        r = requests.get(f"{BASE_URL}/download", timeout=5)
        filename = f"drop_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        with open(filename, "w", newline="") as f:
            f.write(r.text)
        status_text.set_text(f"Status: Saved to {filename}")
        status_text.set_color("#00ffff")
        print(f"Saved to {filename}")
    except Exception as e:
        status_text.set_text(f"Save error: {e}")
        status_text.set_color("#ff0000")

btn_start.on_clicked(on_start)
btn_stop.on_clicked(on_stop)
btn_save.on_clicked(on_save)

# ── Animation ─────────────────────────────────────────────────────────────────
def update(frame):
    with lock:
        if len(t_data) < 2:
            return lines
        t  = list(t_data)
        ys = [ax_data, ay_data, az_data, gx_data, gy_data, gz_data]

    for i, line in enumerate(lines):
        line.set_data(t, ys[i])
        ax_plots[i].relim()
        ax_plots[i].autoscale_view()

    return lines

ani = animation.FuncAnimation(fig, update, interval=100, blit=False, cache_frame_data=False)

plt.show()