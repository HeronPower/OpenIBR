#!/usr/bin/env python3
"""
Simple UART Message Sender for LaunchXL_F28P65x
Sends command messages to COM3
"""

import serial
import struct
import time
import sys

class UARTMessageSender:
    def __init__(self, port='COM3', baudrate=115200):
        """Initialize UART sender"""
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        
    def connect(self):
        """Connect to the serial port"""
        try:
            self.serial_conn = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=1
            )
            print(f"Connected to {self.port} at {self.baudrate} baud")
            return True
        except serial.SerialException as e:
            print(f"Error connecting to {self.port}: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from the serial port"""
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
            print(f"Disconnected from {self.port}")
    
    def send_message(self, enable, P_request, Q_request, V_ref, F_ref, F_nominal=50.0,
                      scale_dac1=1.0, offset_dac1=0.0, signal_ID_dac1=0,
                      scale_dac2=1.0, offset_dac2=0.0, signal_ID_dac2=0):
        """
        Send a message with 5 base values (2 bytes each) plus 2 configurable
        debug-DAC channels (scale: float, offset: float, signal_ID: unsigned)

        Args:
            enable (int): Enable flag (0 or 1)
            P_request (float): Active power request in kW (signed)
            Q_request (float): Reactive power request in kVAR (signed)
            V_ref (float): Voltage reference in Volts (unsigned)
            F_ref (float): Frequency reference in Hz
            F_nominal (float): Nominal frequency in Hz (default 50.0)
            scale_dac1 (float): Scale applied to the signal driving debug DAC 1
            offset_dac1 (float): Offset applied to the signal driving debug DAC 1
            signal_ID_dac1 (int): SIGNAL_ID (see signal_ids_autogen.h) to output on debug DAC 1
            scale_dac2 (float): Scale applied to the signal driving debug DAC 2
            offset_dac2 (float): Offset applied to the signal driving debug DAC 2
            signal_ID_dac2 (int): SIGNAL_ID (see signal_ids_autogen.h) to output on debug DAC 2
        """
        if not self.serial_conn or not self.serial_conn.is_open:
            print("Not connected to serial port")
            return False

        try:
            # Process values for transmission
            enable_val = max(0, min(1, int(enable)))

            # P and Q as signed values in kW (no clamping)
            P_val = int(P_request*1e-3)  # Convert W to kW as signed integer
            Q_val = int(Q_request*1e-3)  # Convert VA to kVA as signed integer

            # V as unsigned value in V (no clamping)
            V_val = int(V_ref)

            # F as signed value relative to nominal in mHz
            F_delta_mHz = int((F_ref - F_nominal) * 1000)  # Difference from nominal in mHz

            # Pack: enable(H), P(h), Q(h), V(H), F_delta(h),
            #       scale_dac1(f), offset_dac1(f), signal_ID_dac1(H),
            #       scale_dac2(f), offset_dac2(f), signal_ID_dac2(H)
            message = struct.pack('<HhhHhffHffH',
                                   enable_val, P_val, Q_val, V_val, F_delta_mHz,
                                   scale_dac1, offset_dac1, signal_ID_dac1,
                                   scale_dac2, offset_dac2, signal_ID_dac2)

            # Send the message
            self.serial_conn.write(message)
            self.serial_conn.flush()

            print(f"Sent: Enable={enable_val}, P={P_request}kW, Q={Q_request}kVAR, V={V_val}V, F={F_ref}Hz (d{F_delta_mHz}mHz), "
                  f"DAC1=[id={signal_ID_dac1}, scale={scale_dac1}, offset={offset_dac1}], "
                  f"DAC2=[id={signal_ID_dac2}, scale={scale_dac2}, offset={offset_dac2}]")
            print(f"Bytes: {message.hex()}")
            return True

        except Exception as e:
            print(f"Error sending message: {e}")
            return False
    
    def read_response(self):
        """Read response from the device"""
        if not self.serial_conn or not self.serial_conn.is_open:
            return None
        
        try:
            if self.serial_conn.in_waiting > 0:
                response = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                return response
        except Exception as e:
            print(f"Error reading response: {e}")
        
        return None

def main():
    """Main function to demonstrate UART message sending"""
    sender = UARTMessageSender()
    
    if not sender.connect():
        print("Failed to connect. Exiting.")
        sys.exit(1)
    
    try:
        print("Simple UART Message Sender")
        print("Press Ctrl+C to exit")
        print("-" * 50)
        
        # Test messages with realistic values
        messages = [
            # (enable, P_request_kW, Q_request_kVAR, V_ref_V, F_ref_Hz)
            (0, 0.0, 0.0, 0, 50.0),         # Disable/Stop
            (1, 500e3, 200e3, 35e3, 60.0),      # Normal operation at nominal frequency
            (1, -500e3, -200e3, 35e3, 60.5),   # Regenerative, slightly low frequency
            (1, 250e3, 200e3, 35e3, 59.5),      # Higher power, slightly high frequency
        ]
        
        for i, (enable, P, Q, V, F) in enumerate(messages):
            print(f"\nSending message {i+1}/{len(messages)}...")
            
            if sender.send_message(enable, P, Q, V, F):
                # Wait and check for response
                time.sleep(0.2)
                response = sender.read_response()
                if response:
                    print(f"Response: {response}")
            
            time.sleep(1)  # Wait between messages
        
        print("\nDemo completed!")
        
    except KeyboardInterrupt:
        print("\nInterrupted by user")
    finally:
        sender.disconnect()

if __name__ == "__main__":
    main() 