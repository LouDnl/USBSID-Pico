#!/usr/bin/env python3
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import configparser
import subprocess
import os
import threading
from pathlib import Path
from sys import argv, platform

class USBSidPicoConfigGUI:# {}
    def __init__(self, root):
        self.root = root
        self.root.title("USBSID-Pico Config Tool GUI v0.1 ~ by ISL/Samar")
        self.root.geometry("800x800")

        # Ścieżki - względne do lokalizacji tego pliku
        script_dir = os.path.dirname(os.path.abspath(__file__))
        if platform == "linux" or platform == "linux2" or platform == "darwin":
            toolexe = "cfg_usbsid"
        elif platform == "win32":
            toolexe = "cfg_usbsid.exe"
        argc = len(argv)
        if (argc > 1):
            toolini = argv[1]
        else:
            toolini = "default.ini"
        if not ".ini" in toolini:
            toolini = "default.ini"
        self.exe_path = os.path.join(script_dir, toolexe)
        self.default_ini_path = os.path.join(script_dir, toolini)
        # self.exe_path = os.path.join(script_dir, "cfg_usbsid.exe")
        # self.default_ini_path = os.path.join(script_dir, "default.ini")

        # Sprawdź czy pliki istnieją
        if not os.path.exists(self.exe_path):
            messagebox.showerror("Error", f"cfg_usbsid.exe not found at: {self.exe_path}")
        if not os.path.exists(self.default_ini_path):
            messagebox.showwarning("Warning", f"default.ini not found at: {self.default_ini_path}")

        # Konfiguracja
        self.config = configparser.ConfigParser()
        self.current_ini_path = self.default_ini_path

        self.setup_ui()
        self.load_config()

    def setup_ui(self):
        # Main notebook
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill='both', expand=True, padx=10, pady=10)

        # Tabs
        self.setup_general_tab()
        self.setup_socket_tab()
        self.setup_led_tab()
        self.setup_actions_tab()
        self.setup_console_tab()

    def setup_general_tab(self):
        general_frame = ttk.Frame(self.notebook)
        self.notebook.add(general_frame, text="General")

        # General controls
        ttk.Label(general_frame, text="General Configuration", font=('Arial', 12, 'bold')).pack(pady=10)

        # Clock rate
        clock_frame = ttk.LabelFrame(general_frame, text="SID Clock Rate")
        clock_frame.pack(fill='x', padx=10, pady=5)

        self.clock_var = tk.StringVar(value="1000000")
        clock_rates = [("1.000 MHz", "1000000"), ("0.985 MHz", "985248"),
                      ("1.023 MHz", "1022727"), ("1.023 MHz (Alt)", "1023440")]

        for text, value in clock_rates:
            ttk.Radiobutton(clock_frame, text=text, variable=self.clock_var,
                           value=value).pack(anchor='w', padx=10, pady=2)

        # Lock clock rate
        self.lock_clock_var = tk.BooleanVar()
        ttk.Checkbutton(general_frame, text="Lock clock rate",
                       variable=self.lock_clock_var).pack(pady=5)

        # Audio switch
        audio_frame = ttk.LabelFrame(general_frame, text="Audio Switch")
        audio_frame.pack(fill='x', padx=10, pady=5)

        self.audio_var = tk.StringVar(value="Stereo")
        ttk.Radiobutton(audio_frame, text="Mono", variable=self.audio_var,
                       value="Mono").pack(anchor='w', padx=10, pady=2)
        ttk.Radiobutton(audio_frame, text="Stereo", variable=self.audio_var,
                       value="Stereo").pack(anchor='w', padx=10, pady=2)

        self.lock_audio_var = tk.BooleanVar()
        ttk.Checkbutton(audio_frame, text="Lock audio switch",
                       variable=self.lock_audio_var).pack(pady=5)

        # FMOPL
        fmopl_frame = ttk.LabelFrame(general_frame, text="FMOPL")
        fmopl_frame.pack(fill='x', padx=10, pady=5)

        self.fmopl_var = tk.BooleanVar()
        ttk.Checkbutton(fmopl_frame, text="Enable FMOPL", variable=self.fmopl_var).pack(pady=5)

    def setup_socket_tab(self):
        socket_frame = ttk.Frame(self.notebook)
        self.notebook.add(socket_frame, text="SID Sockets")

        # Create a frame to hold both sockets side by side
        sockets_container = ttk.Frame(socket_frame)
        sockets_container.pack(fill='both', expand=True, padx=10, pady=5)

        # Socket 1
        socket1_frame = ttk.LabelFrame(sockets_container, text="Socket 1")
        socket1_frame.pack(side='left', fill='both', expand=True, padx=(0, 5))

        self.socket1_enabled = tk.BooleanVar(value=True)
        ttk.Checkbutton(socket1_frame, text="Enabled", variable=self.socket1_enabled).pack(anchor='w', padx=10, pady=2)

        self.socket1_dualsid = tk.StringVar(value="Disabled")
        ttk.Label(socket1_frame, text="Dual SID:").pack(anchor='w', padx=10, pady=2)
        ttk.Radiobutton(socket1_frame, text="Disabled", variable=self.socket1_dualsid,
                       value="Disabled").pack(anchor='w', padx=20, pady=1)
        ttk.Radiobutton(socket1_frame, text="Enabled", variable=self.socket1_dualsid,
                       value="Enabled").pack(anchor='w', padx=20, pady=1)

        self.socket1_chiptype = tk.StringVar(value="Real")
        ttk.Label(socket1_frame, text="Chip type:").pack(anchor='w', padx=10, pady=2)
        ttk.Radiobutton(socket1_frame, text="Real", variable=self.socket1_chiptype,
                       value="Real").pack(anchor='w', padx=20, pady=1)
        ttk.Radiobutton(socket1_frame, text="Clone", variable=self.socket1_chiptype,
                       value="Clone").pack(anchor='w', padx=20, pady=1)

        self.socket1_clonetype = tk.StringVar(value="Disabled")
        ttk.Label(socket1_frame, text="Clone type:").pack(anchor='w', padx=10, pady=2)
        clone_types = ["Disabled", "Other", "SKPico", "ARMSID", "FPGASID", "RedipSID"]
        for clone_type in clone_types:
            ttk.Radiobutton(socket1_frame, text=clone_type, variable=self.socket1_clonetype,
                           value=clone_type).pack(anchor='w', padx=20, pady=1)

        self.socket1_sid1type = tk.StringVar(value="MOS8580")
        ttk.Label(socket1_frame, text="SID 1 type:").pack(anchor='w', padx=10, pady=2)
        sid_types = ["Unknown", "N/A", "MOS8580", "MOS6581", "FMopl"]
        for sid_type in sid_types:
            ttk.Radiobutton(socket1_frame, text=sid_type, variable=self.socket1_sid1type,
                           value=sid_type).pack(anchor='w', padx=20, pady=1)

        self.socket1_sid2type = tk.StringVar(value="N/A")
        ttk.Label(socket1_frame, text="SID 2 type:").pack(anchor='w', padx=10, pady=2)
        for sid_type in sid_types:
            ttk.Radiobutton(socket1_frame, text=sid_type, variable=self.socket1_sid2type,
                           value=sid_type).pack(anchor='w', padx=20, pady=1)

        # Socket 2
        socket2_frame = ttk.LabelFrame(sockets_container, text="Socket 2")
        socket2_frame.pack(side='right', fill='both', expand=True, padx=(5, 0))

        self.socket2_enabled = tk.BooleanVar(value=True)
        ttk.Checkbutton(socket2_frame, text="Enabled", variable=self.socket2_enabled).pack(anchor='w', padx=10, pady=2)

        self.socket2_dualsid = tk.StringVar(value="Disabled")
        ttk.Label(socket2_frame, text="Dual SID:").pack(anchor='w', padx=10, pady=2)
        ttk.Radiobutton(socket2_frame, text="Disabled", variable=self.socket2_dualsid,
                       value="Disabled").pack(anchor='w', padx=20, pady=1)
        ttk.Radiobutton(socket2_frame, text="Enabled", variable=self.socket2_dualsid,
                       value="Enabled").pack(anchor='w', padx=20, pady=1)

        self.socket2_chiptype = tk.StringVar(value="Real")
        ttk.Label(socket2_frame, text="Chip type:").pack(anchor='w', padx=10, pady=2)
        ttk.Radiobutton(socket2_frame, text="Real", variable=self.socket2_chiptype,
                       value="Real").pack(anchor='w', padx=20, pady=1)
        ttk.Radiobutton(socket2_frame, text="Clone", variable=self.socket2_chiptype,
                       value="Clone").pack(anchor='w', padx=20, pady=1)

        self.socket2_clonetype = tk.StringVar(value="Disabled")
        ttk.Label(socket2_frame, text="Clone type:").pack(anchor='w', padx=10, pady=2)
        for clone_type in clone_types:
            ttk.Radiobutton(socket2_frame, text=clone_type, variable=self.socket2_clonetype,
                           value=clone_type).pack(anchor='w', padx=20, pady=1)

        self.socket2_sid1type = tk.StringVar(value="MOS8580")
        ttk.Label(socket2_frame, text="SID 1 type:").pack(anchor='w', padx=10, pady=2)
        for sid_type in sid_types:
            ttk.Radiobutton(socket2_frame, text=sid_type, variable=self.socket2_sid1type,
                           value=sid_type).pack(anchor='w', padx=20, pady=1)

        self.socket2_sid2type = tk.StringVar(value="N/A")
        ttk.Label(socket2_frame, text="SID 2 type:").pack(anchor='w', padx=10, pady=2)
        for sid_type in sid_types:
            ttk.Radiobutton(socket2_frame, text=sid_type, variable=self.socket2_sid2type,
                           value=sid_type).pack(anchor='w', padx=20, pady=1)

        self.socket2_act_as_one = tk.BooleanVar(value=True)
        ttk.Checkbutton(socket2_frame, text="Act as one (mirror socket 1)",
                       variable=self.socket2_act_as_one).pack(pady=5)

    def setup_led_tab(self):
        led_frame = ttk.Frame(self.notebook)
        self.notebook.add(led_frame, text="LED")

        # LED
        led_config_frame = ttk.LabelFrame(led_frame, text="LED")
        led_config_frame.pack(fill='x', padx=10, pady=5)

        self.led_enabled = tk.BooleanVar(value=True)
        ttk.Checkbutton(led_config_frame, text="Enabled", variable=self.led_enabled).pack(anchor='w', padx=10, pady=2)

        self.led_breathe = tk.BooleanVar(value=True)
        ttk.Checkbutton(led_config_frame, text="Idle breathing", variable=self.led_breathe).pack(anchor='w', padx=10, pady=2)

        # RGB LED
        rgb_frame = ttk.LabelFrame(led_frame, text="RGB LED")
        rgb_frame.pack(fill='x', padx=10, pady=5)

        self.rgb_enabled = tk.BooleanVar(value=True)
        ttk.Checkbutton(rgb_frame, text="Enabled", variable=self.rgb_enabled).pack(anchor='w', padx=10, pady=2)

        self.rgb_breathe = tk.BooleanVar(value=True)
        ttk.Checkbutton(rgb_frame, text="Idle breathing", variable=self.rgb_breathe).pack(anchor='w', padx=10, pady=2)

        # Brightness
        ttk.Label(rgb_frame, text="Brightness (0-255):").pack(anchor='w', padx=10, pady=2)
        self.rgb_brightness = tk.Scale(rgb_frame, from_=0, to=255, orient='horizontal')
        self.rgb_brightness.set(1)
        self.rgb_brightness.pack(fill='x', padx=10, pady=2)

        # SID to use
        ttk.Label(rgb_frame, text="SID to use:").pack(anchor='w', padx=10, pady=2)
        self.rgb_sid_to_use = tk.StringVar(value="1")
        for i in range(1, 5):
            ttk.Radiobutton(rgb_frame, text=f"SID {i}", variable=self.rgb_sid_to_use,
                           value=str(i)).pack(anchor='w', padx=20, pady=1)

    def setup_actions_tab(self):
        actions_frame = ttk.Frame(self.notebook)
        self.notebook.add(actions_frame, text="Actions")

        # Presets
        presets_frame = ttk.LabelFrame(actions_frame, text="Presets")
        presets_frame.pack(fill='x', padx=10, pady=5)

        preset_buttons = [
            ("Single SID", "--single-sid"),
            ("Dual SID", "--dual-sid"),
            ("Dual SID Socket 1", "--dual-sid-socket1"),
            ("Dual SID Socket 2", "--dual-sid-socket2"),
            ("Triple SID 1", "--triple-sid1"),
            ("Triple SID 2", "--triple-sid2"),
            ("Quad SID", "--quad-sid"),
            ("Mirrored SID", "--mirrored-sid")
        ]

        for i, (text, command) in enumerate(preset_buttons):
            row = i // 2
            col = i % 2
            ttk.Button(presets_frame, text=text,
                      command=lambda cmd=command: self.run_command(cmd)).grid(row=row, column=col, padx=5, pady=2, sticky='ew')

        # Device actions
        device_frame = ttk.LabelFrame(actions_frame, text="Device Actions")
        device_frame.pack(fill='x', padx=10, pady=5)

        # Create a frame for device buttons and results
        device_content_frame = ttk.Frame(device_frame)
        device_content_frame.pack(fill='both', expand=True, padx=5, pady=5)

        # Left side - buttons
        device_buttons_frame = ttk.Frame(device_content_frame)
        device_buttons_frame.pack(side='left', fill='y', padx=(0, 10))

        device_buttons = [
            ("Read Configuration", "--read-config"),
            ("Read Version", "--version"),
            ("Read Clock Speed", "--read-clock-speed"),
            ("Reset SID", "--reset-sids"),
            ("Reset SID Registers", "--reset-sid-registers"),
            ("Reboot USBSID-Pico", "--reboot-usp"),
            ("Bootloader for FW update", "--bootloader"),
            ("Auto-detect All", "--auto-detect-all"),
            ("Apply Configuration", "--apply-config"),
            ("Save Configuration", "--save-config"),
            ("Save and Reboot", "--save-reboot")
        ]

        for i, (text, command) in enumerate(device_buttons):
            ttk.Button(device_buttons_frame, text=text,
                      command=lambda cmd=command: self.run_device_command(cmd)).pack(fill='x', padx=5, pady=2)

        # Right side - results display
        device_results_frame = ttk.LabelFrame(device_content_frame, text="Device Results")
        device_results_frame.pack(side='right', fill='both', expand=True)

        # Results text area
        self.device_results_text = tk.Text(device_results_frame, height=15, width=50)
        device_scrollbar = ttk.Scrollbar(device_results_frame, orient="vertical", command=self.device_results_text.yview)
        self.device_results_text.configure(yscrollcommand=device_scrollbar.set)

        self.device_results_text.pack(side="left", fill="both", expand=True, padx=5, pady=5)
        device_scrollbar.pack(side="right", fill="y")

        # Clear results button
        ttk.Button(device_results_frame, text="Clear Results",
                  command=self.clear_device_results).pack(pady=2)

        # File actions
        file_frame = ttk.LabelFrame(actions_frame, text="File Actions")
        file_frame.pack(fill='x', padx=10, pady=5)

        ttk.Button(file_frame, text="Generate Default INI",
                  command=self.generate_default_ini).pack(fill='x', padx=5, pady=2)
        ttk.Button(file_frame, text="Export Configuration",
                  command=self.export_config).pack(fill='x', padx=5, pady=2)
        ttk.Button(file_frame, text="Import Configuration",
                  command=self.import_config).pack(fill='x', padx=5, pady=2)
        ttk.Button(file_frame, text="Save Settings to INI",
                  command=self.save_to_ini).pack(fill='x', padx=5, pady=2)
        ttk.Button(file_frame, text="Load Settings from INI",
                  command=self.load_from_ini).pack(fill='x', padx=5, pady=2)

    def setup_console_tab(self):
        console_frame = ttk.Frame(self.notebook)
        self.notebook.add(console_frame, text="Console")

        # Console output
        self.console_text = tk.Text(console_frame, height=20, width=80)
        scrollbar = ttk.Scrollbar(console_frame, orient="vertical", command=self.console_text.yview)
        self.console_text.configure(yscrollcommand=scrollbar.set)

        self.console_text.pack(side="left", fill="both", expand=True, padx=10, pady=10)
        scrollbar.pack(side="right", fill="y")

        # Clear button
        ttk.Button(console_frame, text="Clear Console",
                  command=self.clear_console).pack(pady=5)

    def log_to_console(self, message):
        self.console_text.insert(tk.END, f"{message}\n")
        self.console_text.see(tk.END)

    def clear_console(self):
        self.console_text.delete(1.0, tk.END)

    def run_command(self, command):
        def run():
            try:
                self.log_to_console(f"Executing: {command}")
                result = subprocess.run([self.exe_path, command],
                                      capture_output=True, text=True, timeout=10)

                if result.stdout:
                    self.log_to_console(f"STDOUT: {result.stdout}")
                if result.stderr:
                    self.log_to_console(f"STDERR: {result.stderr}")

                self.log_to_console(f"Exit code: {result.returncode}")

            except subprocess.TimeoutExpired:
                self.log_to_console("Command timed out")
            except Exception as e:
                self.log_to_console(f"Error: {e}")

        threading.Thread(target=run, daemon=True).start()

    def run_device_command(self, command):
        """Run device command and display results in device results area"""
        def run():
            try:
                self.log_to_device_results(f"Executing: {command}")
                result = subprocess.run([self.exe_path, command],
                                      capture_output=True, text=True, timeout=10)

                if result.stdout:
                    self.log_to_device_results(f"STDOUT:\n{result.stdout}")
                if result.stderr:
                    self.log_to_device_results(f"STDERR:\n{result.stderr}")

                self.log_to_device_results(f"Exit code: {result.returncode}")

            except subprocess.TimeoutExpired:
                self.log_to_device_results("Command timed out")
            except Exception as e:
                self.log_to_device_results(f"Error: {e}")

        threading.Thread(target=run, daemon=True).start()

    def log_to_device_results(self, message):
        """Log message to device results area"""
        self.device_results_text.insert(tk.END, f"{message}\n")
        self.device_results_text.see(tk.END)

    def clear_device_results(self):
        """Clear device results area"""
        self.device_results_text.delete(1.0, tk.END)

    def load_config(self):
        try:
            if os.path.exists(self.current_ini_path):
                self.config.read(self.current_ini_path)
                self.update_ui_from_config()
                self.log_to_console(f"Loaded configuration from: {self.current_ini_path}")
            else:
                self.log_to_console(f"Configuration file does not exist: {self.current_ini_path}")
        except Exception as e:
            self.log_to_console(f"Error loading configuration: {e}")

    def update_ui_from_config(self):
        try:
            # General
            if 'General' in self.config:
                gen = self.config['General']
                self.clock_var.set(gen.get('clock_rate', '1000000'))
                self.lock_clock_var.set(gen.get('lock_clockrate', 'False').lower() == 'true')

            # Socket 1
            if 'socketOne' in self.config:
                s1 = self.config['socketOne']
                self.socket1_enabled.set(s1.get('enabled', 'True').lower() == 'true')
                self.socket1_dualsid.set(s1.get('dualsid', 'Disabled'))
                self.socket1_chiptype.set(s1.get('chiptype', 'Real'))
                self.socket1_clonetype.set(s1.get('clonetype', 'Disabled'))
                self.socket1_sid1type.set(s1.get('sid1type', 'MOS8580'))
                self.socket1_sid2type.set(s1.get('sid2type', 'N/A'))

            # Socket 2
            if 'socketTwo' in self.config:
                s2 = self.config['socketTwo']
                self.socket2_enabled.set(s2.get('enabled', 'True').lower() == 'true')
                self.socket2_dualsid.set(s2.get('dualsid', 'Disabled'))
                self.socket2_chiptype.set(s2.get('chiptype', 'Real'))
                self.socket2_clonetype.set(s2.get('clonetype', 'Disabled'))
                self.socket2_sid1type.set(s2.get('sid1type', 'MOS8580'))
                self.socket2_sid2type.set(s2.get('sid2type', 'N/A'))
                self.socket2_act_as_one.set(s2.get('act_as_one', 'True').lower() == 'true')

            # LED
            if 'LED' in self.config:
                led = self.config['LED']
                self.led_enabled.set(led.get('enabled', 'True').lower() == 'true')
                self.led_breathe.set(led.get('idle_breathe', 'True').lower() == 'true')

            # RGB LED
            if 'RGBLED' in self.config:
                rgb = self.config['RGBLED']
                self.rgb_enabled.set(rgb.get('enabled', 'True').lower() == 'true')
                self.rgb_breathe.set(rgb.get('idle_breathe', 'True').lower() == 'true')
                self.rgb_brightness.set(int(rgb.get('brightness', '1')))
                self.rgb_sid_to_use.set(rgb.get('sid_to_use', '1'))

            # FMOPL
            if 'FMOPL' in self.config:
                self.fmopl_var.set(self.config['FMOPL'].get('enabled', 'False').lower() == 'true')

            # Audio switch
            if 'Audioswitch' in self.config:
                audio = self.config['Audioswitch']
                self.audio_var.set(audio.get('set_to', 'Stereo'))
                self.lock_audio_var.set(audio.get('lock_audio_switch', 'False').lower() == 'true')

        except Exception as e:
            self.log_to_console(f"Error updating UI: {e}")

    def save_to_ini(self):
        try:
            # General
            if 'General' not in self.config:
                self.config['General'] = {}

            self.config['General']['clock_rate'] = self.clock_var.get()
            self.config['General']['lock_clockrate'] = str(self.lock_clock_var.get())

            # Socket 1
            if 'socketOne' not in self.config:
                self.config['socketOne'] = {}

            self.config['socketOne']['enabled'] = str(self.socket1_enabled.get())
            self.config['socketOne']['dualsid'] = self.socket1_dualsid.get()
            self.config['socketOne']['chiptype'] = self.socket1_chiptype.get()
            self.config['socketOne']['clonetype'] = self.socket1_clonetype.get()
            self.config['socketOne']['sid1type'] = self.socket1_sid1type.get()
            self.config['socketOne']['sid2type'] = self.socket1_sid2type.get()

            # Socket 2
            if 'socketTwo' not in self.config:
                self.config['socketTwo'] = {}

            self.config['socketTwo']['enabled'] = str(self.socket2_enabled.get())
            self.config['socketTwo']['dualsid'] = self.socket2_dualsid.get()
            self.config['socketTwo']['chiptype'] = self.socket2_chiptype.get()
            self.config['socketTwo']['clonetype'] = self.socket2_clonetype.get()
            self.config['socketTwo']['sid1type'] = self.socket2_sid1type.get()
            self.config['socketTwo']['sid2type'] = self.socket2_sid2type.get()
            self.config['socketTwo']['act_as_one'] = str(self.socket2_act_as_one.get())

            # LED
            if 'LED' not in self.config:
                self.config['LED'] = {}

            self.config['LED']['enabled'] = str(self.led_enabled.get())
            self.config['LED']['idle_breathe'] = str(self.led_breathe.get())

            # RGB LED
            if 'RGBLED' not in self.config:
                self.config['RGBLED'] = {}

            self.config['RGBLED']['enabled'] = str(self.rgb_enabled.get())
            self.config['RGBLED']['idle_breathe'] = str(self.rgb_breathe.get())
            self.config['RGBLED']['brightness'] = str(self.rgb_brightness.get())
            self.config['RGBLED']['sid_to_use'] = self.rgb_sid_to_use.get()

            # FMOPL
            if 'FMOPL' not in self.config:
                self.config['FMOPL'] = {}

            self.config['FMOPL']['enabled'] = str(self.fmopl_var.get())

            # Audio switch
            if 'Audioswitch' not in self.config:
                self.config['Audioswitch'] = {}

            self.config['Audioswitch']['set_to'] = self.audio_var.get()
            self.config['Audioswitch']['lock_audio_switch'] = str(self.lock_audio_var.get())

            # Save to file
            with open(self.current_ini_path, 'w') as configfile:
                self.config.write(configfile)

            self.log_to_console(f"Saved configuration to: {self.current_ini_path}")
            messagebox.showinfo("Success", "Configuration has been saved")

        except Exception as e:
            self.log_to_console(f"Error saving: {e}")
            messagebox.showerror("Error", f"Failed to save configuration: {e}")

    def load_from_ini(self):
        file_path = filedialog.askopenfilename(
            title="Select INI file",
            filetypes=[("INI files", "*.ini"), ("All files", "*.*")]
        )

        if file_path:
            self.current_ini_path = file_path
            self.load_config()

    def generate_default_ini(self):
        self.run_command("--default-ini")

    def export_config(self):
        file_path = filedialog.asksaveasfilename(
            title="Export Configuration",
            defaultextension=".ini",
            filetypes=[("INI files", "*.ini"), ("All files", "*.*")]
        )

        if file_path:
            self.run_command(f"--export-config {file_path}")

    def import_config(self):
        file_path = filedialog.askopenfilename(
            title="Import Configuration",
            filetypes=[("INI files", "*.ini"), ("All files", "*.*")]
        )

        if file_path:
            self.run_command(f"--import-config {file_path}")

def main():
    root = tk.Tk()
    app = USBSidPicoConfigGUI(root)
    root.mainloop()

if __name__ == "__main__":
    main()
