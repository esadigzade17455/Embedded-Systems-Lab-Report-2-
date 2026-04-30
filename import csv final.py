import csv
import os
import threading
from datetime import datetime
import tkinter as tk
from tkinter import ttk, messagebox

import serial
import serial.tools.list_ports
import matplotlib.pyplot as plt


# Folder where all player CSV files will be stored
DATA_DIR = "player_data"
os.makedirs(DATA_DIR, exist_ok=True)


class ReactionGameGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Two Player Reaction Game")
        self.root.geometry("700x520")

        # Serial connection
        self.ser = None
        self.reader_thread = None
        self.running = False

        # Player info
        self.player1 = ""
        self.player2 = ""

        # Scores
        self.score1 = 0
        self.score2 = 0

        # Build interface and detect ports
        self.build_ui()
        self.refresh_ports()

    # -------------------- UI SETUP --------------------
    def build_ui(self):
        main = ttk.Frame(self.root, padding=12)
        main.pack(fill="both", expand=True)

        # ---------- Connection section ----------
        top = ttk.LabelFrame(main, text="Connection", padding=10)
        top.pack(fill="x", pady=6)

        self.port_var = tk.StringVar()

        # Dropdown of COM ports
        self.ports_combo = ttk.Combobox(top, textvariable=self.port_var, width=25, state="readonly")
        self.ports_combo.grid(row=0, column=0, padx=5, pady=5)

        ttk.Button(top, text="Refresh Ports", command=self.refresh_ports).grid(row=0, column=1, padx=5)
        ttk.Button(top, text="Connect", command=self.connect_serial).grid(row=0, column=2, padx=5)
        ttk.Button(top, text="Disconnect", command=self.disconnect_serial).grid(row=0, column=3, padx=5)

        self.connection_label = ttk.Label(top, text="Not connected")
        self.connection_label.grid(row=0, column=4, padx=10)

        # ---------- Player input ----------
        players = ttk.LabelFrame(main, text="Players", padding=10)
        players.pack(fill="x", pady=6)

        ttk.Label(players, text="Player 1 Name:").grid(row=0, column=0, sticky="w", padx=5)
        self.p1_entry = ttk.Entry(players, width=20)
        self.p1_entry.grid(row=0, column=1, padx=5)

        ttk.Label(players, text="Player 2 Name:").grid(row=0, column=2, sticky="w", padx=5)
        self.p2_entry = ttk.Entry(players, width=20)
        self.p2_entry.grid(row=0, column=3, padx=5)

        ttk.Button(players, text="Start Match", command=self.start_match).grid(row=0, column=4, padx=8)
        ttk.Button(players, text="Reset Arduino", command=self.reset_arduino).grid(row=0, column=5, padx=8)

        # ---------- Status display ----------
        status = ttk.LabelFrame(main, text="Live Status", padding=10)
        status.pack(fill="x", pady=6)

        self.status_var = tk.StringVar(value="Waiting...")
        ttk.Label(status, textvariable=self.status_var, font=("Arial", 12)).pack(anchor="w")

        self.score_var = tk.StringVar(value="Score: 0 - 0")
        ttk.Label(status, textvariable=self.score_var, font=("Arial", 12, "bold")).pack(anchor="w")

        # ---------- Log output ----------
        self.result_box = tk.Text(main, height=12, width=80)
        self.result_box.pack(fill="both", expand=True, pady=8)
        self.result_box.insert("end", "Log output will appear here...\n")
        self.result_box.config(state="disabled")

        # ---------- Statistics ----------
        stats = ttk.LabelFrame(main, text="Statistics", padding=10)
        stats.pack(fill="x", pady=6)

        ttk.Label(stats, text="Player name for graph:").grid(row=0, column=0)
        self.stats_entry = ttk.Entry(stats, width=20)
        self.stats_entry.grid(row=0, column=1)

        ttk.Button(stats, text="Show Reaction Times", command=self.show_reaction_times).grid(row=0, column=2)
        ttk.Button(stats, text="Show Win Rate by Opponent", command=self.show_win_rate).grid(row=0, column=3)

    # -------------------- LOGGING --------------------
    def log(self, text):
        """Print text into GUI log box"""
        self.result_box.config(state="normal")
        self.result_box.insert("end", text + "\n")
        self.result_box.see("end")
        self.result_box.config(state="disabled")

    # -------------------- SERIAL PORTS --------------------
    def refresh_ports(self):
        """Refresh available COM ports"""
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.ports_combo["values"] = ports
        if ports:
            self.ports_combo.current(0)

    def connect_serial(self):
        """Connect to Arduino via selected port"""
        port = self.port_var.get().strip()
        if not port:
            messagebox.showerror("Error", "Choose a COM port first.")
            return

        try:
            self.ser = serial.Serial(port, 115200, timeout=1)

            # Start background thread to read Arduino data
            self.running = True
            self.reader_thread = threading.Thread(target=self.read_serial_loop, daemon=True)
            self.reader_thread.start()

            self.connection_label.config(text=f"Connected: {port}")
            self.log(f"Connected to {port}")

        except Exception as e:
            messagebox.showerror("Connection Error", str(e))

    def disconnect_serial(self):
        """Disconnect from Arduino"""
        self.running = False
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.connection_label.config(text="Not connected")
        self.log("Disconnected.")

    def send_line(self, line):
        """Send command to Arduino"""
        if not self.ser or not self.ser.is_open:
            messagebox.showerror("Error", "Arduino is not connected.")
            return
        self.ser.write((line + "\n").encode())

    # -------------------- GAME CONTROL --------------------
    def start_match(self):
        """Start a new match"""
        self.player1 = self.p1_entry.get().strip() or "Player1"
        self.player2 = self.p2_entry.get().strip() or "Player2"

        self.score1 = 0
        self.score2 = 0
        self.score_var.set("Score: 0 - 0")
        self.status_var.set("Match started")

        cmd = f"START,{self.player1},{self.player2}"
        self.send_line(cmd)
        self.log(f"Sent: {cmd}")

    def reset_arduino(self):
        """Reset Arduino game state"""
        self.send_line("RESET")
        self.status_var.set("Arduino reset requested")
        self.score1 = 0
        self.score2 = 0
        self.score_var.set("Score: 0 - 0")

    # -------------------- SERIAL READING --------------------
    def read_serial_loop(self):
        """Continuously read data from Arduino in background"""
        while self.running:
            try:
                if self.ser and self.ser.is_open:
                    raw = self.ser.readline().decode(errors="ignore").strip()
                    if raw:
                        # Update GUI safely from main thread
                        self.root.after(0, self.handle_serial_line, raw)
            except Exception as e:
                self.root.after(0, self.log, f"Serial error: {e}")
                break

    def handle_serial_line(self, line):
        """Process messages received from Arduino"""
        self.log(f"Arduino: {line}")

        parts = line.split(",")
        tag = parts[0]

        if tag == "READY":
            self.status_var.set("Arduino ready")

        elif tag == "MATCH_STARTED":
            self.status_var.set(f"Match: {parts[1]} vs {parts[2]}")

        elif tag == "SCORE":
            self.score1 = int(parts[1])
            self.score2 = int(parts[2])
            self.score_var.set(f"Score: {self.score1} - {self.score2}")

        elif tag == "ROUND_START":
            delay_ms = int(parts[1])
            self.status_var.set(f"Random wait = {delay_ms/1000:.2f}s")

        elif tag == "BUZZER_ON":
            self.status_var.set("BUZZER! Press now!")

        elif tag == "ROUND_RESULT":
            winner = int(parts[1])
            p1_time = int(parts[2])
            p2_time = int(parts[3])
            false_start = bool(int(parts[4]))
            false_starter = int(parts[5])

            self.score1 = int(parts[6])
            self.score2 = int(parts[7])
            self.score_var.set(f"Score: {self.score1} - {self.score2}")

            winner_name = self.player1 if winner == 1 else self.player2

            if false_start:
                fs_name = self.player1 if false_starter == 1 else self.player2
                self.status_var.set(f"False start by {fs_name}. Point to {winner_name}")
            else:
                self.status_var.set(f"Round winner: {winner_name}")

            # Save round data
            self.save_round_result(
                self.player1, self.player2, winner_name,
                p1_time, p2_time, false_start, false_starter,
                self.score1, self.score2
            )

        elif tag == "MATCH_WINNER":
            winner_name = parts[1]
            self.status_var.set(f"MATCH WINNER: {winner_name}")

            # Save match result
            self.save_match_result(self.player1, self.player2, winner_name, self.score1, self.score2)

    # -------------------- FILE HANDLING --------------------
    def player_file(self, player_name):
        """Return CSV file path for a player"""
        safe = player_name.replace(" ", "_")
        return os.path.join(DATA_DIR, f"{safe}.csv")

    def save_round_result(self, p1, p2, winner, p1_time, p2_time, false_start, false_starter, score1, score2):
        """Save each round data into CSV"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # Save for both players
        rows = [
            {"timestamp": timestamp, "player": p1, "opponent": p2, "role": "P1",
             "winner": winner, "reaction_time_ms": p1_time,
             "false_start": int(false_start and false_starter == 1),
             "score1": score1, "score2": score2, "match_complete": 0},

            {"timestamp": timestamp, "player": p2, "opponent": p1, "role": "P2",
             "winner": winner, "reaction_time_ms": p2_time,
             "false_start": int(false_start and false_starter == 2),
             "score1": score1, "score2": score2, "match_complete": 0}
        ]

        for row in rows:
            file_path = self.player_file(row["player"])
            file_exists = os.path.exists(file_path)

            with open(file_path, "a", newline="", encoding="utf-8") as f:
                writer = csv.DictWriter(f, fieldnames=row.keys())
                if not file_exists:
                    writer.writeheader()
                writer.writerow(row)

    # -------------------- STATISTICS --------------------
    def show_reaction_times(self):
        """Plot reaction time graph for a player"""
        player = self.stats_entry.get().strip()

        file_path = self.player_file(player)
        if not os.path.exists(file_path):
            messagebox.showerror("Error", "No data found.")
            return

        times = []
        labels = []

        with open(file_path, "r", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            idx = 1
            for row in reader:
                rt = row["reaction_time_ms"].strip()
                if rt and rt != "0":
                    times.append(int(rt))
                    labels.append(idx)
                    idx += 1

        plt.plot(labels, times, marker="o")
        plt.title(f"Reaction Times for {player}")
        plt.xlabel("Round")
        plt.ylabel("ms")
        plt.grid(True)
        plt.show()

    def show_win_rate(self):
        """Plot win rate against different opponents"""
        player = self.stats_entry.get().strip()

        file_path = self.player_file(player)
        if not os.path.exists(file_path):
            messagebox.showerror("Error", "No data found.")
            return

        stats = {}

        with open(file_path, "r", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                if row["match_complete"] != "1":
                    continue

                opponent = row["opponent"]
                winner = row["winner"]

                if opponent not in stats:
                    stats[opponent] = {"wins": 0, "matches": 0}

                stats[opponent]["matches"] += 1
                if winner == player:
                    stats[opponent]["wins"] += 1

        opponents = list(stats.keys())
        win_rates = [
            (stats[o]["wins"] / stats[o]["matches"]) * 100
            for o in opponents
        ]

        plt.bar(opponents, win_rates)
        plt.title(f"Win Rate for {player}")
        plt.ylabel("%")
        plt.show()


# -------------------- MAIN --------------------
if __name__ == "__main__":
    root = tk.Tk()
    app = ReactionGameGUI(root)
    root.mainloop()
