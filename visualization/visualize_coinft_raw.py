#!/usr/bin/env python3
"""CoinFT raw-channel viewer for a direct PSoC or Teensy serial stream."""

import threading
import time
import tkinter as tk
from collections import deque
from tkinter import messagebox, ttk

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
import numpy as np
import serial
import serial.tools.list_ports


CHANNELS = 12
HISTORY = 500
DIRECT_BAUD_RATE = 1_000_000
BRIDGE_BAUD_RATE = 115200
DIRECT_FRAME_SIZE = 26
DIRECT_START = 0x02
DIRECT_END = 0x03

MODES = {
    "PSoC direct (02...03)": "direct",
    "Teensy framed (00 00...)": "bridge",
    "Fixed raw bytes": "fixed",
}


class CoinFTRawViewer:
    def __init__(self, root):
        self.root = root
        self.root.title("CoinFT Raw Waveform Viewer")
        self.root.geometry("1180x820")

        self.ser = None
        self.reader = None
        self.stop_event = threading.Event()
        self.lock = threading.Lock()
        self.samples = deque(maxlen=HISTORY)
        self.lines = []
        self.axes = []

        self.port_var = tk.StringVar()
        self.count_var = tk.StringVar(value="1")
        self.mode_var = tk.StringVar(value="PSoC direct (02...03)")
        self.status_var = tk.StringVar(value="Stopped")

        self._create_controls()
        self._create_plot()
        self.refresh_ports()
        self.root.after(50, self.update_plot)

    def _create_controls(self):
        frame = ttk.Frame(self.root, padding=8)
        frame.pack(fill="x")

        ttk.Label(frame, text="Port").pack(side="left")
        self.port_box = ttk.Combobox(frame, textvariable=self.port_var, width=32, state="readonly")
        self.port_box.pack(side="left", padx=(6, 12))
        ttk.Button(frame, text="Refresh", command=self.refresh_ports).pack(side="left")

        ttk.Label(frame, text="Sensors").pack(side="left", padx=(18, 4))
        self.count_box = ttk.Combobox(frame, textvariable=self.count_var, values=("1", "2"), width=5, state="readonly")
        self.count_box.pack(side="left")

        ttk.Label(frame, text="Frame format").pack(side="left", padx=(18, 4))
        self.mode_box = ttk.Combobox(frame, textvariable=self.mode_var, values=tuple(MODES), width=20, state="readonly")
        self.mode_box.pack(side="left")

        self.start_button = ttk.Button(frame, text="Start", command=self.start)
        self.start_button.pack(side="left", padx=(18, 4))
        self.stop_button = ttk.Button(frame, text="Stop", command=self.stop, state="disabled")
        self.stop_button.pack(side="left")
        ttk.Label(frame, textvariable=self.status_var).pack(side="right")

    def _create_plot(self):
        self.figure = Figure(figsize=(10, 7), dpi=100)
        self.canvas = FigureCanvasTkAgg(self.figure, master=self.root)
        self.canvas.get_tk_widget().pack(fill="both", expand=True, padx=8, pady=(0, 8))
        self._reset_plot(1)

    def _reset_plot(self, count):
        self.figure.clear()
        self.axes = list(self.figure.subplots(count, 1, squeeze=False)[:, 0])
        colors = plt_colors(count=CHANNELS)
        self.lines = []
        for sensor_index, ax in enumerate(self.axes):
            sensor_lines = []
            for channel_index, color in enumerate(colors):
                line, = ax.plot([], [], color=color, linewidth=1, label=f"Ch {channel_index}")
                sensor_lines.append(line)
            self.lines.append(sensor_lines)
            ax.set_title(f"CoinFT {sensor_index + 1} Raw Channels")
            ax.set_ylabel("Raw count")
            ax.grid(True)
            ax.legend(loc="upper left", ncol=4, fontsize="small")
        self.axes[-1].set_xlabel("Sample")
        self.figure.tight_layout()
        self.canvas.draw_idle()

    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_box["values"] = ports
        preferred = "/dev/cu.usbmodem14403"
        if preferred in ports:
            self.port_var.set(preferred)
        elif ports:
            self.port_var.set(ports[0])

    def start(self):
        if self.ser and self.ser.is_open:
            return

        port = self.port_var.get()
        count = int(self.count_var.get())
        mode = MODES[self.mode_var.get()]
        if mode == "direct" and count != 1:
            messagebox.showerror("Configuration error", "PSoC direct mode carries one 12-channel sensor.")
            return
        if not port:
            messagebox.showerror("Connection error", "Select a serial port first.")
            return

        try:
            baud = DIRECT_BAUD_RATE if mode == "direct" else BRIDGE_BAUD_RATE
            self.ser = serial.Serial(port, baud, timeout=0.1)
            self.ser.write(b"i")
            time.sleep(0.2)
            self.ser.reset_input_buffer()
            if mode == "direct":
                self.ser.write(b"q")
                if self.ser.read(1) != b"\x1a":
                    raise RuntimeError("PSoC did not answer q with 0x1A")
            self.ser.write(b"s")
        except Exception as exc:
            messagebox.showerror("Connection error", str(exc))
            self.ser = None
            return

        self.samples.clear()
        self._reset_plot(count)
        self.stop_event.clear()
        self.reader = threading.Thread(
            target=self.read_stream,
            args=(count, mode),
            daemon=True,
        )
        self.reader.start()
        self.start_button.config(state="disabled")
        self.stop_button.config(state="normal")
        self.port_box.config(state="disabled")
        self.count_box.config(state="disabled")
        self.mode_box.config(state="disabled")
        self.status_var.set(f"Streaming {count} sensor(s)")

    def stop(self):
        self.stop_event.set()
        if self.ser and self.ser.is_open:
            try:
                self.ser.write(b"i")
                self.ser.close()
            except serial.SerialException:
                pass
        self.ser = None
        self.start_button.config(state="normal")
        self.stop_button.config(state="disabled")
        self.port_box.config(state="readonly")
        self.count_box.config(state="readonly")
        self.mode_box.config(state="readonly")
        self.status_var.set("Stopped")

    def read_stream(self, count, mode):
        body_len = count * CHANNELS * 2
        stream_buffer = bytearray()
        while not self.stop_event.is_set() and self.ser and self.ser.is_open:
            if mode == "direct":
                stream_buffer.extend(self.ser.read(128))
                while len(stream_buffer) >= DIRECT_FRAME_SIZE:
                    start = stream_buffer.find(bytes((DIRECT_START,)))
                    if start < 0:
                        stream_buffer.clear()
                        break
                    if start:
                        del stream_buffer[:start]
                    if len(stream_buffer) < DIRECT_FRAME_SIZE:
                        break
                    candidate = bytes(stream_buffer[:DIRECT_FRAME_SIZE])
                    if candidate[-1] != DIRECT_END:
                        del stream_buffer[0]
                        continue
                    del stream_buffer[:DIRECT_FRAME_SIZE]
                    values = np.frombuffer(candidate[1:-1], dtype="<u2").astype(np.float64)
                    frame = values.reshape(1, CHANNELS)
                    with self.lock:
                        self.samples.append(frame)
                continue

            if mode == "bridge":
                if self.ser.read(1) != b"\x00" or self.ser.read(1) != b"\x00":
                    continue
            body = self.ser.read(body_len)
            if len(body) != body_len:
                continue
            values = np.frombuffer(body, dtype="<u2").astype(np.float64)
            frame = values.reshape(count, CHANNELS)
            with self.lock:
                self.samples.append(frame)

    def update_plot(self):
        with self.lock:
            data = np.asarray(list(self.samples))

        if data.ndim == 3 and data.shape[0] > 0:
            x = np.arange(data.shape[0])
            for sensor_index, ax in enumerate(self.axes):
                for channel_index, line in enumerate(self.lines[sensor_index]):
                    line.set_data(x, data[:, sensor_index, channel_index])
                ax.relim()
                ax.autoscale_view()
            self.canvas.draw_idle()

        self.root.after(50, self.update_plot)

    def close(self):
        self.stop()
        self.root.destroy()


def plt_colors(count):
    import matplotlib.pyplot as plt
    return plt.cm.tab20(np.linspace(0, 1, count))


if __name__ == "__main__":
    app_root = tk.Tk()
    app = CoinFTRawViewer(app_root)
    app_root.protocol("WM_DELETE_WINDOW", app.close)
    app_root.mainloop()
