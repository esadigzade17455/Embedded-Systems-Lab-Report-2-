import sqlite3          # Database engine (stores RFID tags locally)
import serial           # Communication with Arduino (UART/USB)
import threading        # Runs serial reading without freezing GUI
import queue            # Safe communication between threads
import tkinter as tk    # GUI library
from tkinter import ttk, messagebox  # GUI widgets + popups

DB_FILE = "rfid_tags.db"   # SQLite database file name


# ================= DATABASE CLASS =================
# Handles storing RFID tag data (UID, time, count)
class RFIDDatabase:
    def __init__(self, db_file):
        self.db_file = db_file
        self.create()   # Create table if not exists

    # Create a new database connection each time
    def connect(self):
        return sqlite3.connect(self.db_file)

    # Create table structure
    def create(self):
            conn = self.connect()
            cur = conn.cursor()

            cur.execute("""
             CREATE TABLE IF NOT EXISTS tags (
                 id INTEGER PRIMARY KEY AUTOINCREMENT,
                 uid TEXT UNIQUE,
                 last_seen TEXT,
                 count INTEGER DEFAULT 1
              )
            """)
            #Works offline
            #Automatically created when program runs

            conn.commit()
            conn.close()


    # Insert new tag or update existing one
    def update(self, uid):
        import datetime
        now = datetime.datetime.now().strftime("%H:%M:%S")

        conn = self.connect()
        cur = conn.cursor()

        # Check if UID already exists
        cur.execute("SELECT count FROM tags WHERE uid=?", (uid,))
        row = cur.fetchone()

        if row:
            # If exists → increase scan count + update time
            cur.execute(
                "UPDATE tags SET count=count+1, last_seen=? WHERE uid=?",
                (now, uid)
            )
        else:
            # If new → insert into database
            cur.execute(
                "INSERT INTO tags(uid,last_seen,count) VALUES (?,?,1)",
                (uid, now)
            )

        conn.commit()
        conn.close()


# ================= MAIN GUI APPLICATION =================
class App:
    def __init__(self, root):
        self.root = root
        self.root.title("Security System Monitor (FINAL)")
        self.root.geometry("1000x650")

        self.db = RFIDDatabase(DB_FILE)  # connect database

        # Serial communication variables
        self.serial = None
        self.running = False

        # Thread-safe queue (used to pass serial data to GUI)
        self.q = queue.Queue()

        # Live system states
        self.state = "UNKNOWN"
        self.keypad = ""
        self.ir = ""

        self.build()                 # build GUI
        self.root.after(100, self.update)  # start update loop

    # ================= UI DESIGN =================
    def build(self):
        top = ttk.Frame(self.root)
        top.pack(fill=tk.X)

        # Serial port input field
        self.port = ttk.Entry(top, width=30)
        self.port.pack(side=tk.LEFT, padx=5)
        self.port.insert(0, "/dev/cu.usbmodem1401")  # default Arduino port

        # Buttons
        ttk.Button(top, text="CONNECT", command=self.connect).pack(side=tk.LEFT)
        ttk.Button(top, text="DISCONNECT", command=self.disconnect).pack(side=tk.LEFT)

        # Status labels
        self.state_lbl = tk.Label(self.root, text="STATE: UNKNOWN", font=("Arial", 18))
        self.state_lbl.pack(pady=10)

        self.kp_lbl = tk.Label(self.root, text="KEYPAD: ", font=("Arial", 14))
        self.kp_lbl.pack()

        self.ir_lbl = tk.Label(self.root, text="IR: ", font=("Arial", 14))
        self.ir_lbl.pack()

        # Log window (shows raw Arduino messages)
        self.log = tk.Text(self.root, height=18)
        self.log.pack(fill=tk.BOTH, expand=True)

        # Table for RFID data
        self.tree = ttk.Treeview(self.root, columns=("uid", "count"), show="headings")
        self.tree.heading("uid", text="UID")
        self.tree.heading("count", text="COUNT")
        self.tree.pack(fill=tk.BOTH, expand=True)

    # ================= SERIAL CONNECTION =================
    def connect(self):
        try:
            # Open serial connection with Arduino
            self.serial = serial.Serial(self.port.get(), 9600, timeout=1)
            self.running = True

            # Start background thread for reading serial data
            threading.Thread(target=self.reader, daemon=True).start()

        except Exception as e:
            messagebox.showerror("Error", str(e))

    # Stop serial communication
    def disconnect(self):
        self.running = False
        if self.serial:
            self.serial.close()

    # Background thread function
    # Continuously reads data from Arduino and gets UID from arduino
    def reader(self):
        while self.running:
            try:
                line = self.serial.readline().decode(errors="ignore").strip() # takes data from arduino
                if line:
                    self.q.put(line)   # send to main thread safely
            except:
                break

    # ================= MAIN UPDATE LOOP =================
    def update(self):
        # Process all incoming serial messages
        while not self.q.empty():
            line = self.q.get() # what type of data

            # Show raw data in GUI log
            self.log.insert(tk.END, line + "\n")
            self.log.see(tk.END)

            # ---------- SYSTEM STATE ----------
            if "SYS,LOCKED" in line:
                self.state = "LOCKED 🔴"
                self.keypad = ""
                self.ir = ""

            if "SYS,UNLOCKED" in line:
                self.state = "UNLOCKED 🟢"
                self.keypad = ""
                self.ir = ""

            # ---------- KEYPAD INPUT ----------
            if "KEYPAD:" in line:
                key = line.split(":")[1].strip()
                self.keypad += key + " "

            if "KP_DIGIT:" in line:
                key = line.split(":")[1].strip()
                self.keypad += "[" + key + "] "

            if "KP_CLEAR" in line:
                self.keypad = ""

            # ---------- IR SENSOR ----------
            if "IR_DIGIT:" in line:
                self.ir += line.split(":")[1].strip() + " "

            if "IR_WRONG" in line:
                self.ir = ""

            if "IR_OK" in line:
                self.ir = ""

            # ---------- RFID TAG ----------
            if line.startswith("TAG,"): # detect rfid 
                uid = line.split(",")[1] # take uid 

                # store/update database
                self.db.update(uid) # save data to db

                # refresh table UI
                self.refresh()

            # update labels
            self.refresh_ui()

        # loop again after 100ms
        self.root.after(100, self.update)

    # ================= UPDATE LABELS =================
    def refresh_ui(self):
        self.state_lbl.config(text=f"STATE: {self.state}")
        self.kp_lbl.config(text=f"KEYPAD: {self.keypad}")
        self.ir_lbl.config(text=f"IR: {self.ir}")

    # ================= UPDATE TABLE =================
    def refresh(self):
        # clear table
        for i in self.tree.get_children():
            self.tree.delete(i)

        # load data from database
        conn = sqlite3.connect(DB_FILE)
        cur = conn.cursor()
        cur.execute("SELECT uid, count FROM tags")

        # insert into GUI table
        for row in cur.fetchall():
            self.tree.insert("", tk.END, values=row)

        conn.close()


# ================= START APPLICATION =================
if __name__ == "__main__":
    root = tk.Tk()
    App(root)
    root.mainloop()