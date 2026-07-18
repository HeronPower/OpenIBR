#!/usr/bin/env python3
"""
UART Message Sender GUI for LaunchXL_F28P65x
Simple GUI with 5 edit boxes and send button
"""

import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import struct
import threading

class UARTSenderGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("UART Message Sender")
        self.root.geometry("450x560")
        
        # Serial connection
        self.serial_conn = None
        self.baudrate = 115200
        
        self.create_widgets()
        self.refresh_ports()
        
    def create_widgets(self):
        # Main frame
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # COM Port selection frame
        port_frame = ttk.LabelFrame(main_frame, text="Serial Connection", padding="5")
        port_frame.grid(row=0, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=(0, 10))
        
        # COM Port dropdown
        ttk.Label(port_frame, text="COM Port:").grid(row=0, column=0, sticky=tk.W, padx=(0, 5))
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(port_frame, textvariable=self.port_var, width=15, state="readonly")
        self.port_combo.grid(row=0, column=1, sticky=(tk.W, tk.E), padx=(0, 5))
        
        # Refresh button
        self.refresh_button = ttk.Button(port_frame, text="Refresh", command=self.refresh_ports)
        self.refresh_button.grid(row=0, column=2, padx=(0, 5))
        
        # Connect/Disconnect button
        self.connect_button = ttk.Button(port_frame, text="Connect", command=self.toggle_connection)
        self.connect_button.grid(row=0, column=3)
        
        # Connection status
        self.status_label = ttk.Label(main_frame, text="Disconnected", foreground="red")
        self.status_label.grid(row=1, column=0, columnspan=2, pady=(0, 10))
        
        # Input fields
        ttk.Label(main_frame, text="Enable (0/1):").grid(row=2, column=0, sticky=tk.W, pady=2)
        self.enable_var = tk.StringVar(value="1")
        self.enable_entry = ttk.Entry(main_frame, textvariable=self.enable_var, width=15)
        self.enable_entry.grid(row=2, column=1, sticky=(tk.W, tk.E), pady=2)
        
        ttk.Label(main_frame, text="P Request (W):").grid(row=3, column=0, sticky=tk.W, pady=2)
        self.p_var = tk.StringVar(value="15000")
        self.p_entry = ttk.Entry(main_frame, textvariable=self.p_var, width=15)
        self.p_entry.grid(row=3, column=1, sticky=(tk.W, tk.E), pady=2)
        
        ttk.Label(main_frame, text="Q Request (VAR):").grid(row=4, column=0, sticky=tk.W, pady=2)
        self.q_var = tk.StringVar(value="5000")
        self.q_entry = ttk.Entry(main_frame, textvariable=self.q_var, width=15)
        self.q_entry.grid(row=4, column=1, sticky=(tk.W, tk.E), pady=2)
        
        ttk.Label(main_frame, text="V Reference (V):").grid(row=5, column=0, sticky=tk.W, pady=2)
        self.v_var = tk.StringVar(value="400")
        self.v_entry = ttk.Entry(main_frame, textvariable=self.v_var, width=15)
        self.v_entry.grid(row=5, column=1, sticky=(tk.W, tk.E), pady=2)
        
        ttk.Label(main_frame, text="F Delta (Hz):").grid(row=6, column=0, sticky=tk.W, pady=2)
        self.f_var = tk.StringVar(value="0.0")
        self.f_entry = ttk.Entry(main_frame, textvariable=self.f_var, width=15)
        self.f_entry.grid(row=6, column=1, sticky=(tk.W, tk.E), pady=2)
        
        # Add helpful note about nominal frequency
        ttk.Label(main_frame, text="(Nominal: 60.0 Hz)", font=("TkDefaultFont", 8)).grid(row=7, column=1, sticky=tk.W, pady=(0, 5))

        # Configurable debug DAC channels
        dac_frame = ttk.LabelFrame(main_frame, text="Configurable Debug DACs", padding="5")
        dac_frame.grid(row=8, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=(0, 10))

        ttk.Label(dac_frame, text="DAC1 Signal ID:").grid(row=0, column=0, sticky=tk.W, pady=2)
        self.signal_id_dac1_var = tk.StringVar(value="0")
        ttk.Entry(dac_frame, textvariable=self.signal_id_dac1_var, width=12).grid(row=0, column=1, sticky=(tk.W, tk.E), pady=2)

        ttk.Label(dac_frame, text="DAC1 Scale:").grid(row=1, column=0, sticky=tk.W, pady=2)
        self.scale_dac1_var = tk.StringVar(value="1.0")
        ttk.Entry(dac_frame, textvariable=self.scale_dac1_var, width=12).grid(row=1, column=1, sticky=(tk.W, tk.E), pady=2)

        ttk.Label(dac_frame, text="DAC1 Offset:").grid(row=2, column=0, sticky=tk.W, pady=2)
        self.offset_dac1_var = tk.StringVar(value="0.0")
        ttk.Entry(dac_frame, textvariable=self.offset_dac1_var, width=12).grid(row=2, column=1, sticky=(tk.W, tk.E), pady=2)

        ttk.Label(dac_frame, text="DAC2 Signal ID:").grid(row=3, column=0, sticky=tk.W, pady=2)
        self.signal_id_dac2_var = tk.StringVar(value="0")
        ttk.Entry(dac_frame, textvariable=self.signal_id_dac2_var, width=12).grid(row=3, column=1, sticky=(tk.W, tk.E), pady=2)

        ttk.Label(dac_frame, text="DAC2 Scale:").grid(row=4, column=0, sticky=tk.W, pady=2)
        self.scale_dac2_var = tk.StringVar(value="1.0")
        ttk.Entry(dac_frame, textvariable=self.scale_dac2_var, width=12).grid(row=4, column=1, sticky=(tk.W, tk.E), pady=2)

        ttk.Label(dac_frame, text="DAC2 Offset:").grid(row=5, column=0, sticky=tk.W, pady=2)
        self.offset_dac2_var = tk.StringVar(value="0.0")
        ttk.Entry(dac_frame, textvariable=self.offset_dac2_var, width=12).grid(row=5, column=1, sticky=(tk.W, tk.E), pady=2)

        dac_frame.columnconfigure(1, weight=1)

        # Send button
        self.send_button = ttk.Button(main_frame, text="Send Message (Enter)", command=self.send_message)
        self.send_button.grid(row=9, column=0, columnspan=2, pady=10)

        # Bind Enter key to send message
        self.root.bind('<Return>', lambda event: self.send_message())
        self.root.bind('<KP_Enter>', lambda event: self.send_message())  # Numpad Enter

        # Response display
        ttk.Label(main_frame, text="Response:").grid(row=10, column=0, sticky=tk.W, pady=(10, 2))
        self.response_var = tk.StringVar(value="No response yet")
        self.response_label = ttk.Label(main_frame, textvariable=self.response_var, foreground="blue")
        self.response_label.grid(row=11, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=2)
        
        # Configure column weights for responsive design
        main_frame.columnconfigure(1, weight=1)
        port_frame.columnconfigure(1, weight=1)
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        
    def refresh_ports(self):
        """Refresh the list of available COM ports"""
        try:
            ports = [port.device for port in serial.tools.list_ports.comports()]
            if not ports:
                ports = ["No ports found"]
            self.port_combo['values'] = ports
            if ports and ports[0] != "No ports found":
                self.port_combo.set(ports[0])
        except Exception as e:
            messagebox.showerror("Error", f"Failed to list ports: {e}")
    
    def toggle_connection(self):
        """Connect or disconnect from the selected COM port"""
        if self.serial_conn and self.serial_conn.is_open:
            self.disconnect_uart()
        else:
            self.connect_uart()
    
    def connect_uart(self):
        """Connect to UART port"""
        if not self.port_var.get() or self.port_var.get() == "No ports found":
            messagebox.showerror("Error", "No COM port selected")
            return False
            
        try:
            self.serial_conn = serial.Serial(
                port=self.port_var.get(),
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=1
            )
            self.status_label.config(text=f"Connected to {self.port_var.get()} at {self.baudrate} baud", foreground="green")
            self.connect_button.config(text="Disconnect")
            return True
        except serial.SerialException as e:
            self.status_label.config(text=f"Error: {e}", foreground="red")
            messagebox.showerror("Connection Error", f"Failed to connect to {self.port_var.get()}: {e}")
            return False
    
    def disconnect_uart(self):
        """Disconnect from UART port"""
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
        self.serial_conn = None
        self.status_label.config(text="Disconnected", foreground="red")
        self.connect_button.config(text="Connect")
    
    def send_message(self):
        """Send message with current values"""
        if not self.serial_conn or not self.serial_conn.is_open:
            messagebox.showerror("Error", "Not connected to serial port")
            return
        
        try:
            # Get values from GUI
            enable = int(self.enable_var.get())
            P_request = float(self.p_var.get())  # In Watts
            Q_request = float(self.q_var.get())  # In VARs
            V_ref = float(self.v_var.get())      # In Volts
            F_delta = float(self.f_var.get())    # Delta frequency in Hz

            # Configurable debug DAC channels
            scale_dac1 = float(self.scale_dac1_var.get())
            offset_dac1 = float(self.offset_dac1_var.get())
            signal_id_dac1 = int(self.signal_id_dac1_var.get())
            scale_dac2 = float(self.scale_dac2_var.get())
            offset_dac2 = float(self.offset_dac2_var.get())
            signal_id_dac2 = int(self.signal_id_dac2_var.get())

            # Convert and validate values
            enable_val = max(0, min(1, int(enable)))

            # Convert P and Q from Watts to kW (for transmission as signed integers)
            P_val = int(P_request * 1e-3)  # Convert W to kW as signed integer
            Q_val = int(Q_request * 1e-3)  # Convert VAR to kVAR as signed integer

            # V as unsigned value in V (no clamping)
            V_val = int(V_ref)

            # F delta converted to mHz for transmission
            F_delta_mHz = int(F_delta * 1000)  # Convert Hz delta to mHz

            # Pack: enable(H), P(h), Q(h), V(H), F_delta(h),
            #       scale_dac1(f), offset_dac1(f), signal_ID_dac1(H),
            #       scale_dac2(f), offset_dac2(f), signal_ID_dac2(H)
            message = struct.pack('<HhhHhffHffH',
                                   enable_val, P_val, Q_val, V_val, F_delta_mHz,
                                   scale_dac1, offset_dac1, signal_id_dac1,
                                   scale_dac2, offset_dac2, signal_id_dac2)

            # Send the message
            self.serial_conn.write(message)
            self.serial_conn.flush()

            # Update response display
            F_nominal = 60.0  # Assuming 60Hz nominal for display
            F_absolute = F_nominal + F_delta
            self.response_var.set(f"Sent: E={enable_val}, P={P_request}W, Q={Q_request}VAR, V={V_val}V, F={F_absolute}Hz (Δ{F_delta}Hz), "
                                   f"DAC1=[id={signal_id_dac1}, x{scale_dac1}+{offset_dac1}], "
                                   f"DAC2=[id={signal_id_dac2}, x{scale_dac2}+{offset_dac2}]")
            
            # Start a thread to read response
            threading.Thread(target=self.read_response, daemon=True).start()
            
        except ValueError as e:
            messagebox.showerror("Input Error", f"Invalid input: {e}")
        except Exception as e:
            messagebox.showerror("Send Error", f"Error sending message: {e}")
    
    def read_response(self):
        """Read response from device (runs in separate thread)"""
        try:
            if self.serial_conn and self.serial_conn.is_open:
                # Wait a bit for response
                import time
                time.sleep(0.1)
                
                if self.serial_conn.in_waiting > 0:
                    response = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                    # Update GUI from thread (schedule on main thread)
                    self.root.after(0, lambda: self.response_var.set(f"Device response: {response}"))
                else:
                    self.root.after(0, lambda: self.response_var.set("No response received"))
        except Exception as e:
            self.root.after(0, lambda: self.response_var.set(f"Error reading response: {e}"))
    
    def on_closing(self):
        """Handle window closing"""
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
        self.root.destroy()

def main():
    root = tk.Tk()
    app = UARTSenderGUI(root)
    
    # Handle window closing
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    
    # Start the GUI
    root.mainloop()

if __name__ == "__main__":
    main()