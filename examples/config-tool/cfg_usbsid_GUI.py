#!/usr/bin/env python3
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import configparser
import subprocess
import os
import threading
import shlex
import tempfile
from sys import argv, platform # <-- CHANGE: Added import for platform check

class USBSidPicoConfigGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("USBSID-Pico Config Tool GUI v0.2.1 ~ by ISL/Samar")
        self.root.geometry("800x800")

        style = ttk.Style(self.root)
        # --- CHANGE --- Define the appearance of the button in the new 'selected' state
        style.map('Active.TButton',
          font=[('selected', ('Arial', 10, 'bold'))],
          relief=[('selected', 'raised')],
          highlightbackground=[('selected', 'gray')],
            highlightcolor=[('selected', 'gray')],
          borderwidth=[('selected', 7)],
          background=[('selected', 'lightgreen')],
          foreground=[('selected', 'green')])

        script_dir = os.path.dirname(os.path.abspath(__file__))

        # --- START OF CHANGE: Multi-platform logic implementation ---
        if platform == "linux" or platform == "linux2" or platform == "darwin":
            toolexe = "cfg_usbsid"
        elif platform == "win32":
            toolexe = "cfg_usbsid.exe"
        else:
            # Default value for other, unforeseen systems
            toolexe = "cfg_usbsid"

        argc = len(argv)
        if (argc > 1):
            toolini = argv[1]
        else:
            toolini = "default.ini"
        if not ".ini" in toolini:
            toolini = "default.ini"

        self.exe_path = os.path.join(script_dir, toolexe)
        self.default_ini_path = os.path.join(script_dir, toolini)

        # Sprawdź czy pliki istnieją
        if not os.path.exists(self.exe_path):
            messagebox.showerror("Error", f"{toolexe} not found at: {self.exe_path}")
            root.destroy()
            return
        # --- END OF CHANGE ---

        self.config = configparser.ConfigParser()
        self.preset_buttons = {}
        self.current_ini_path = self.default_ini_path

        self.setup_ui()
        self._initial_load()

    def setup_ui(self):
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill='both', expand=True, padx=10, pady=10)
        self.setup_general_tab()
        self.setup_socket_tab()
        self.setup_led_tab()
        self.setup_actions_tab()
        self.setup_console_tab()

    def setup_general_tab(self):
        general_frame = ttk.Frame(self.notebook)
        self.notebook.add(general_frame, text="General")
        ttk.Label(general_frame, text="General Configuration", font=('Arial', 12, 'bold')).pack(pady=10)
        clock_frame = ttk.LabelFrame(general_frame, text="SID Clock Rate")
        clock_frame.pack(fill='x', padx=10, pady=5)
        self.clock_var = tk.StringVar(value="1000000")
        clock_rates = [("1.000 MHz", "1000000"), ("0.985 MHz", "985248"),
                       ("1.023 MHz", "1022727"), ("1.023 MHz (Alt)", "1023440")]
        for text, value in clock_rates:
            ttk.Radiobutton(clock_frame, text=text, variable=self.clock_var, value=value).pack(anchor='w', padx=10, pady=2)
        self.lock_clock_var = tk.BooleanVar()
        ttk.Checkbutton(general_frame, text="Lock clock rate", variable=self.lock_clock_var).pack(pady=5)
        audio_frame = ttk.LabelFrame(general_frame, text="Audio Switch")
        audio_frame.pack(fill='x', padx=10, pady=5)
        self.audio_var = tk.StringVar(value="Stereo")
        ttk.Radiobutton(audio_frame, text="Mono", variable=self.audio_var, value="Mono").pack(anchor='w', padx=10, pady=2)
        ttk.Radiobutton(audio_frame, text="Stereo", variable=self.audio_var, value="Stereo").pack(anchor='w', padx=20, pady=1)
        self.lock_audio_var = tk.BooleanVar()
        ttk.Checkbutton(audio_frame, text="Lock audio switch", variable=self.lock_audio_var).pack(pady=5)
        fmopl_frame = ttk.LabelFrame(general_frame, text="FMOPL")
        fmopl_frame.pack(fill='x', padx=10, pady=5)
        self.fmopl_var = tk.BooleanVar()
        ttk.Checkbutton(fmopl_frame, text="Enable FMOPL", variable=self.fmopl_var).pack(pady=5)

    def setup_socket_tab(self):
        socket_frame = ttk.Frame(self.notebook)
        self.notebook.add(socket_frame, text="SID Sockets")
        sockets_container = ttk.Frame(socket_frame)
        sockets_container.pack(fill='both', expand=True, padx=10, pady=5)
        clone_types = ["Disabled", "Other", "SKPico", "ARMSID", "FPGASID", "RedipSID"]
        sid_types = ["Unknown", "N/A", "MOS8580", "MOS6581", "FMopl"]
        self.socket1_vars = self._create_socket_widgets(sockets_container, "Socket 1", clone_types, sid_types)
        self.socket1_vars["frame"].pack(side='left', fill='both', expand=True, padx=(0, 5))
        self.socket2_vars = self._create_socket_widgets(sockets_container, "Socket 2", clone_types, sid_types)
        self.socket2_vars["frame"].pack(side='right', fill='both', expand=True, padx=(5, 0))
        self.socket2_act_as_one = tk.BooleanVar(value=True)
        ttk.Checkbutton(self.socket2_vars["frame"], text="Act as one (mirror socket 1)", variable=self.socket2_act_as_one).pack(pady=5)

    def _create_socket_widgets(self, parent, title, clone_types, sid_types):
        frame = ttk.LabelFrame(parent, text=title)
        vars = {"frame": frame, "enabled": tk.BooleanVar(value=True), "dualsid": tk.StringVar(value="Disabled"),
                "chiptype": tk.StringVar(value="Real"), "clonetype": tk.StringVar(value="Disabled"),
                "sid1type": tk.StringVar(value="MOS8580"), "sid2type": tk.StringVar(value="N/A")}
        ttk.Checkbutton(frame, text="Enabled", variable=vars["enabled"]).pack(anchor='w', padx=10, pady=2)
        ttk.Label(frame, text="Dual SID:").pack(anchor='w', padx=10, pady=2)
        ttk.Radiobutton(frame, text="Disabled", variable=vars["dualsid"], value="Disabled").pack(anchor='w', padx=20, pady=1)
        ttk.Radiobutton(frame, text="Enabled", variable=vars["dualsid"], value="Enabled").pack(anchor='w', padx=20, pady=1)
        ttk.Label(frame, text="Chip type:").pack(anchor='w', padx=10, pady=2)
        ttk.Radiobutton(frame, text="Real", variable=vars["chiptype"], value="Real").pack(anchor='w', padx=20, pady=1)
        ttk.Radiobutton(frame, text="Clone", variable=vars["chiptype"], value="Clone").pack(anchor='w', padx=20, pady=1)
        ttk.Label(frame, text="Clone type:").pack(anchor='w', padx=10, pady=2)
        for t in clone_types: ttk.Radiobutton(frame, text=t, variable=vars["clonetype"], value=t).pack(anchor='w', padx=20, pady=1)
        ttk.Label(frame, text="SID 1 type:").pack(anchor='w', padx=10, pady=2)
        for t in sid_types: ttk.Radiobutton(frame, text=t, variable=vars["sid1type"], value=t).pack(anchor='w', padx=20, pady=1)
        ttk.Label(frame, text="SID 2 type:").pack(anchor='w', padx=10, pady=2)
        for t in sid_types: ttk.Radiobutton(frame, text=t, variable=vars["sid2type"], value=t).pack(anchor='w', padx=20, pady=1)
        return vars

    def setup_led_tab(self):
        led_frame = ttk.Frame(self.notebook)
        self.notebook.add(led_frame, text="LED")
        led_config_frame = ttk.LabelFrame(led_frame, text="LED")
        led_config_frame.pack(fill='x', padx=10, pady=5)
        self.led_enabled = tk.BooleanVar(value=True)
        ttk.Checkbutton(led_config_frame, text="Enabled", variable=self.led_enabled).pack(anchor='w', padx=10, pady=2)
        self.led_breathe = tk.BooleanVar(value=True)
        ttk.Checkbutton(led_config_frame, text="Idle breathing", variable=self.led_breathe).pack(anchor='w', padx=10, pady=2)
        rgb_frame = ttk.LabelFrame(led_frame, text="RGB LED")
        rgb_frame.pack(fill='x', padx=10, pady=5)
        self.rgb_enabled = tk.BooleanVar(value=True)
        ttk.Checkbutton(rgb_frame, text="Enabled", variable=self.rgb_enabled).pack(anchor='w', padx=10, pady=2)
        self.rgb_breathe = tk.BooleanVar(value=True)
        ttk.Checkbutton(rgb_frame, text="Idle breathing", variable=self.rgb_breathe).pack(anchor='w', padx=10, pady=2)
        ttk.Label(rgb_frame, text="Brightness (0-255):").pack(anchor='w', padx=10, pady=2)
        self.rgb_brightness = tk.Scale(rgb_frame, from_=0, to=255, orient='horizontal')
        self.rgb_brightness.set(1)
        self.rgb_brightness.pack(fill='x', padx=10, pady=2)
        ttk.Label(rgb_frame, text="SID to use:").pack(anchor='w', padx=10, pady=2)
        self.rgb_sid_to_use = tk.StringVar(value="1")
        for i in range(1, 5):
            ttk.Radiobutton(rgb_frame, text=f"SID {i}", variable=self.rgb_sid_to_use, value=str(i)).pack(anchor='w', padx=20, pady=1)

    def setup_actions_tab(self):
        actions_frame = ttk.Frame(self.notebook)
        self.notebook.add(actions_frame, text="Actions")

        presets_frame = ttk.LabelFrame(actions_frame, text="Presets")
        presets_frame.pack(fill='x', padx=10, pady=5)
        preset_buttons_data = [("Single SID", "--single-sid"), ("Dual SID", "--dual-sid"), ("Dual SID Socket 1", "--dual-sid-socket1"),
                               ("Dual SID Socket 2", "--dual-sid-socket2"), ("Triple SID 1", "--triple-sid1"), ("Triple SID 2", "--triple-sid2"),
                               ("Quad SID", "--quad-sid"), ("Mirrored SID", "--mirrored-sid")]
        for i, (text, command_tag) in enumerate(preset_buttons_data):
            # --- CHANGE --- All preset buttons now use the new style
            button = ttk.Button(presets_frame, text=text, command=lambda cmd=text: self.apply_preset(cmd), style='Active.TButton')
            button.grid(row=i // 2, column=i % 2, padx=5, pady=2, sticky='ew')
            self.preset_buttons[text] = button

        device_frame = ttk.LabelFrame(actions_frame, text="Device Actions")
        device_frame.pack(fill='x', padx=10, pady=5)
        device_buttons = [("Read Device settings", self.load_from_device),
                          ("Write current settings to Device", self.send_to_device),
                          ("Read Version [console]", lambda: self.run_command_threaded("--version")),
                          ("Start SID configuration autodetect", self.execute_auto_detect),
                          ("Reboot USBSID-Pico", lambda: self.run_command_threaded("--reboot-usp")),
                          ("Save and Reboot", lambda: self.run_command_threaded("--save-reboot"))]
        for text, command in device_buttons: ttk.Button(device_frame, text=text, command=command).pack(fill='x', padx=5, pady=2)

        file_frame = ttk.LabelFrame(actions_frame, text="File Operations")
        file_frame.pack(fill='x', padx=10, pady=5)
        ttk.Button(file_frame, text="Dump settings from device to .ini File", command=self.export_config).pack(fill='x', padx=5, pady=2)
        ttk.Button(file_frame, text="Write settings to Device from .ini File", command=self.import_config).pack(fill='x', padx=5, pady=2)
        ttk.Button(file_frame, text="Export GUI settings to .ini File", command=self.save_to_ini).pack(fill='x', padx=5, pady=2)
        ttk.Button(file_frame, text="Import .ini File to GUI", command=self.load_from_ini).pack(fill='x', padx=5, pady=2)

    def setup_console_tab(self):
        console_frame = ttk.Frame(self.notebook)
        self.notebook.add(console_frame, text="Console")
        self.console_text = tk.Text(console_frame, height=20, width=80, wrap='word')
        scrollbar = ttk.Scrollbar(console_frame, orient="vertical", command=self.console_text.yview)
        self.console_text.configure(yscrollcommand=scrollbar.set)
        self.console_text.pack(side="left", fill="both", expand=True, padx=10, pady=10)
        scrollbar.pack(side="right", fill="y")
        ttk.Button(console_frame, text="Clear Console", command=self.clear_console).pack(pady=5)

    def _initial_load(self):
        self.log_to_console("--- Application Start ---")
        self.log_to_console("Attempting to auto-load configuration from device...")
        temp_filepath = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.ini', encoding='utf-8') as temp_file:
                temp_filepath = temp_file.name

            command = f'--export-config "{temp_filepath}"'
            self.run_command_blocking(command, silent=True)
            self.load_config(temp_filepath)
            messagebox.showinfo("Device Connected", "Successfully connected and loaded settings from the USBSID-Pico device.")
        except Exception as e:
            self.log_to_console(f"Auto-load from device failed. This is normal if the device is not connected. Details: {e}")
            messagebox.showwarning("Device Not Found", "Could not automatically load settings from the USBSID-Pico device.\n\nPlease ensure the device is connected \n\nAnd NOT playing SID music.\n\nLoading default settings from file instead.")
            self.load_config(self.default_ini_path)
        finally:
            if temp_filepath and os.path.exists(temp_filepath):
                os.remove(temp_filepath)

    def apply_preset(self, preset_name):
        self.log_to_console(f"Preset button clicked: {preset_name}")
        self._set_gui_for_preset(preset_name)

        if messagebox.askyesno("Apply Preset", f"Apply the '{preset_name}' preset and write it to the device?"):
            self.send_to_device(confirm=False)
        else:
            self.log_to_console("Preset application cancelled by user. Reverting to last known device state.")
            self.update_ui_from_config()

    def _set_gui_for_preset(self, preset_name):
        preset_map = {
            "Single SID":        (True, False, False, False, False), "Dual SID":          (True, False, True,  False, False),
            "Dual SID Socket 1": (True, True,  False, False, False), "Dual SID Socket 2": (False,False, True,  True,  False),
            "Triple SID 1":      (True, True,  True,  False, False), "Triple SID 2":      (True, False, True,  True,  False),
            "Quad SID":          (True, True,  True,  True,  False), "Mirrored SID":      (True, False, True,  False, True),
        }
        state = preset_map.get(preset_name)
        if not state: return

        s1_en, s1_dual, s2_en, s2_dual, s2_mirror = state
        self.socket1_vars["enabled"].set(s1_en)
        self.socket1_vars["dualsid"].set("Enabled" if s1_dual else "Disabled")
        self.socket2_vars["enabled"].set(s2_en)
        self.socket2_vars["dualsid"].set("Enabled" if s2_dual else "Disabled")
        self.socket2_act_as_one.set(s2_mirror)
        self.log_to_console(f"GUI controls set to '{preset_name}' state.")
        self._update_preset_highlighting()

    def execute_auto_detect(self, confirm=True):
        if confirm and not messagebox.askyesno("Confirm Autodetection", "Are you sure? Current settings in the GUI will no longer match device settings. You must reload the configuration afterwards!"):
            self.log_to_console("Autodetection cancelled by user.")
            return

        self.log_to_console("Attempting send auto detect command to device...")
        try:
            command = f'--auto-detect-all'
            self.run_command_blocking(command)
            if confirm: messagebox.showinfo("Success", "Autodetect command sent!")
        except Exception as e:
            self.log_to_console(f"Operation failed: {e}")
            messagebox.showerror("Error", f"Failed to send autodetect command.\n\nDetails: {e}")

    def load_from_device(self, confirm=True):
        if confirm and not messagebox.askyesno("Confirm Refresh", "Are you sure? All current settings in the GUI will be overwritten by the settings from the device."):
            self.log_to_console("Refresh operation cancelled by user.")
            return

        self.log_to_console("Attempting to refresh settings from device...")
        temp_filepath = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.ini', encoding='utf-8') as temp_file:
                temp_filepath = temp_file.name

            command = f'--export-config "{temp_filepath}"'
            self.run_command_blocking(command)
            self.load_config(temp_filepath)
            if confirm: messagebox.showinfo("Success", "Settings have been successfully refreshed from the device.")
        except Exception as e:
            self.log_to_console(f"Operation failed: {e}")
            messagebox.showerror("Error", f"Failed to refresh settings from device.\n\nDetails: {e}")
        finally:
            if temp_filepath and os.path.exists(temp_filepath): os.remove(temp_filepath)

    def send_to_device(self, confirm=True):
        if confirm and not messagebox.askyesno("Confirm Write", "Are you sure? All settings on the device will be overwritten."):
            self.log_to_console("Write operation cancelled by user.")
            return

        self.log_to_console("Attempting to write current GUI settings to device...")
        temp_filepath = None
        try:
            self._update_config_from_ui()
            with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.ini', encoding='utf-8', newline='') as temp_file:
                temp_filepath = temp_file.name
                self.config.write(temp_file)

            command = f'--import-config "{temp_filepath}"'
            self.run_command_blocking(command)
            messagebox.showinfo("Success", "Successfully wrote GUI settings to the device.")
        except Exception as e:
            self.log_to_console(f"Operation failed: {e}")
            messagebox.showerror("Error", f"Failed to write settings to device.\n\nDetails: {e}")
        finally:
            if temp_filepath and os.path.exists(temp_filepath): os.remove(temp_filepath)

    def log_to_console(self, message):
        self.console_text.insert(tk.END, f"{message}\n")
        self.console_text.see(tk.END)

    def clear_console(self): self.console_text.delete(1.0, tk.END)
    def run_command_threaded(self, command_string): threading.Thread(target=self.run_command_blocking, args=(command_string,), daemon=True).start()

    def run_command_blocking(self, command_string, silent=False):
        try:
            cmd_list = [self.exe_path] + shlex.split(command_string)
            if not silent: self.log_to_console(f"Executing: {cmd_list}")
            result = subprocess.run(cmd_list, capture_output=True, text=True, timeout=15, check=True, encoding='utf-8', errors='ignore')
            if not silent:
                if result.stdout: self.log_to_console(f"STDOUT: {result.stdout}")
                if result.stderr: self.log_to_console(f"STDERR: {result.stderr}")
            return True
        except FileNotFoundError: raise Exception(f"Executable not found at: {self.exe_path}") # CHANGE: Better error message
        except subprocess.TimeoutExpired: raise Exception("The operation timed out. Is the device connected and responsive?")
        except subprocess.CalledProcessError as e:
            error_details = f"Command failed with exit code {e.returncode}.\n\nSTDERR:\n{e.stderr or 'N/A'}\n\nSTDOUT:\n{e.stdout or 'N/A'}"
            raise Exception(error_details)
        except Exception as e: raise Exception(f"An unexpected error occurred during command execution: {e}")

    def load_config(self, file_path):
        try:
            if os.path.exists(file_path):
                config = configparser.ConfigParser()
                config.read(file_path)
                self.config = config
                self.update_ui_from_config()
                self.current_ini_path = file_path
                self.log_to_console(f"Successfully loaded and applied config from: {file_path}")
        except Exception as e: self.log_to_console(f"Error loading INI file {file_path}: {e}")

    def update_ui_from_config(self):
        try:
            if self.config.has_section('General'):
                gen = self.config['General']
                self.clock_var.set(gen.get('clock_rate', '1000000'))
                self.lock_clock_var.set(gen.getboolean('lock_clockrate', fallback=False))
            if self.config.has_section('Audioswitch'):
                self.audio_var.set(self.config['Audioswitch'].get('set_to', 'Stereo'))
                self.lock_audio_var.set(self.config['Audioswitch'].getboolean('lock_audio_switch', fallback=False))
            if self.config.has_section('FMOPL'): self.fmopl_var.set(self.config['FMOPL'].getboolean('enabled', fallback=False))
            if self.config.has_section('socketOne'): self._update_socket_ui(self.socket1_vars, self.config['socketOne'])
            if self.config.has_section('socketTwo'):
                self._update_socket_ui(self.socket2_vars, self.config['socketTwo'])
                self.socket2_act_as_one.set(self.config['socketTwo'].getboolean('act_as_one', fallback=True))
            if self.config.has_section('LED'):
                self.led_enabled.set(self.config['LED'].getboolean('enabled', fallback=True))
                self.led_breathe.set(self.config['LED'].getboolean('idle_breathe', fallback=True))
            if self.config.has_section('RGBLED'):
                rgb = self.config['RGBLED']
                self.rgb_enabled.set(rgb.getboolean('enabled', fallback=True))
                self.rgb_breathe.set(rgb.getboolean('idle_breathe', fallback=True))
                self.rgb_brightness.set(rgb.getint('brightness', fallback=1))
                self.rgb_sid_to_use.set(rgb.get('sid_to_use', '1'))
            self.root.after(50, self._update_preset_highlighting)
        except Exception as e: self.log_to_console(f"CRITICAL ERROR updating UI from config: {e}")

    def _update_preset_highlighting(self):
        # --- CHANGE --- Managing the 'selected' state instead of changing the style
        for button in self.preset_buttons.values():
            button.state(['!selected'])

        preset_map = {
            "Single SID":        (True, False, False, False, False), "Dual SID":          (True, False, True,  False, False),
            "Dual SID Socket 1": (True, True,  False, False, False), "Dual SID Socket 2": (False,False, True,  True,  False),
            "Triple SID 1":      (True, True,  True,  False, False), "Triple SID 2":      (True, False, True,  True,  False),
            "Quad SID":          (True, True,  True,  True,  False), "Mirrored SID":      (True, False, True,  False, True),
        }

        s1_en = self.socket1_vars["enabled"].get()
        s1_dual = self.socket1_vars["dualsid"].get() == 'Enabled'
        s2_en = self.socket2_vars["enabled"].get()
        s2_dual = self.socket2_vars["dualsid"].get() == 'Enabled'
        s2_mirror = self.socket2_act_as_one.get()
        current_state = (s1_en, s1_dual, s2_en, s2_dual, s2_mirror)

        active_preset_name = None
        for name, state in preset_map.items():
            if state == current_state:
                active_preset_name = name
                break

        if active_preset_name and self.preset_buttons.get(active_preset_name):
            self.preset_buttons[active_preset_name].state(['selected'])
            self.log_to_console(f"Active preset detected: {active_preset_name}")

    def _update_socket_ui(self, uivars, config_section):
        uivars["enabled"].set(config_section.getboolean('enabled', fallback=True))
        uivars["dualsid"].set(config_section.get('dualsid', 'Disabled'))
        uivars["chiptype"].set(config_section.get('chiptype', 'Real'))
        uivars["clonetype"].set(config_section.get('clonetype', 'Disabled'))
        uivars["sid1type"].set(config_section.get('sid1type', 'MOS8580'))
        uivars["sid2type"].set(config_section.get('sid2type', 'N/A'))

    def save_to_ini(self):
        try:
            file_path = filedialog.asksaveasfilename(title="Export GUI settings to .ini File", defaultextension=".ini", filetypes=[("INI files", "*.ini"), ("All files", "*.*")])
            if not file_path: return
            self._update_config_from_ui()
            with open(file_path, 'w') as configfile: self.config.write(configfile)
            messagebox.showinfo("Success", f"GUI settings have been exported to\n{file_path}")
        except Exception as e: messagebox.showerror("Error", f"Failed to export configuration: {e}")

    def _update_config_from_ui(self):
        if not self.config.has_section('General'): self.config.add_section('General')
        self.config['General']['clock_rate'] = self.clock_var.get()
        self.config['General']['lock_clockrate'] = str(self.lock_clock_var.get())
        self._update_socket_config(self.socket1_vars, 'socketOne')
        self._update_socket_config(self.socket2_vars, 'socketTwo')
        if not self.config.has_section('socketTwo'): self.config.add_section('socketTwo')
        self.config['socketTwo']['act_as_one'] = str(self.socket2_act_as_one.get())
        if not self.config.has_section('LED'): self.config.add_section('LED')
        self.config['LED']['enabled'] = str(self.led_enabled.get())
        self.config['LED']['idle_breathe'] = str(self.led_breathe.get())
        if not self.config.has_section('RGBLED'): self.config.add_section('RGBLED')
        self.config['RGBLED']['enabled'] = str(self.rgb_enabled.get())
        self.config['RGBLED']['idle_breathe'] = str(self.rgb_breathe.get())
        self.config['RGBLED']['brightness'] = str(self.rgb_brightness.get())
        self.config['RGBLED']['sid_to_use'] = self.rgb_sid_to_use.get()
        if not self.config.has_section('FMOPL'): self.config.add_section('FMOPL')
        self.config['FMOPL']['enabled'] = str(self.fmopl_var.get())
        if not self.config.has_section('Audioswitch'): self.config.add_section('Audioswitch')
        self.config['Audioswitch']['set_to'] = self.audio_var.get()
        self.config['Audioswitch']['lock_audio_switch'] = str(self.lock_audio_var.get())

    def _update_socket_config(self, uivars, section_name):
        if not self.config.has_section(section_name): self.config.add_section(section_name)
        s = self.config[section_name]
        s['enabled'], s['dualsid'], s['chiptype'], s['clonetype'], s['sid1type'], s['sid2type'] = \
            str(uivars["enabled"].get()), uivars["dualsid"].get(), uivars["chiptype"].get(), \
            uivars["clonetype"].get(), uivars["sid1type"].get(), uivars["sid2type"].get()

    def load_from_ini(self):
        file_path = filedialog.askopenfilename(title="Import .ini File to GUI", filetypes=[("INI files", "*.ini"), ("All files", "*.*")])
        if file_path: self.load_config(file_path)

    def export_config(self):
        file_path = filedialog.asksaveasfilename(title="Dump settings from device to .ini File", defaultextension=".ini", filetypes=[("INI files", "*.ini"), ("All files", "*.*")])
        if not file_path: return
        try:
            self.run_command_blocking(f'--export-config "{file_path}"')
            messagebox.showinfo("Success", f"Successfully dumped device configuration to\n{file_path}")
        except Exception as e: messagebox.showerror("Error", f"Failed to dump configuration from device.\n\nDetails: {e}")

    def import_config(self):
        file_path = filedialog.askopenfilename(title="Write settings to Device from .ini File", filetypes=[("INI files", "*.ini"), ("All files", "*.*")])
        if not file_path: return
        try:
            self.run_command_blocking(f'--import-config "{file_path}"')
            messagebox.showinfo("Success", f"Successfully wrote configuration to device from\n{file_path}")
        except Exception as e: messagebox.showerror("Error", f"Failed to write configuration to device.\n\nDetails: {e}")

def main():
    root = tk.Tk()
    app = USBSidPicoConfigGUI(root)
    if app.root.winfo_exists(): root.mainloop()

if __name__ == "__main__":
    main()
