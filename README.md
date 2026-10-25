# SmartFlow — Household Water Usage Monitor

**Know where your water goes, track usage, estimate cost, and detect leaks.**

SmartFlow is an ESP32-based water monitoring prototype that uses a water-flow sensor, a leak sensor, a TFT display, an LED, and a buzzer. It displays readings on the TFT and provides a local web dashboard hosted by the ESP32.

## Features

- Live water-flow rate in litres per minute (L/min)
- Total water usage in litres
- Leak detection using an analog sensor
- LED blinking and buzzer alerts when a leak is detected
- Live web dashboard hosted by the ESP32
- Flow-session start time, stop time, duration, and water usage
- Daily, weekly, and monthly water-use analysis
- Water-cost estimation using a configurable price per litre
- On-device TFT dashboard displaying flow rate, total usage, and leak status

## Hardware Components

| Component | Purpose |
|---|---|
| ESP32 DevKit | Main controller |
| ZJ-S201 Water Flow Sensor | Measures water flow |
| Analog Soil-Moisture Sensor | Detects leaked water |
| 1.8-inch SPI TFT Display (128 × 160) | Displays live readings |
| Active Buzzer | Audible leak alert |
| Red LED | Visual leak alert |
| 220 Ω Resistor | LED current limiting |
| 5 kΩ and 6.2 kΩ Resistors | Flow-sensor signal interface |
| Breadboard and Jumper Wires | Circuit connections |

## Pin Connections

### Water Flow Sensor — ZJ-S201

| Sensor Wire | ESP32 Connection |
|---|---|
| Red / VCC | VIN / 5V |
| Black / GND | GND |
| Yellow / Pulse | GPIO27 through the verified signal interface |

**Important:** Never connect a 5 V sensor signal directly to an ESP32 GPIO. Verify the sensor output type and final resistor circuit before powering the board.

### Leak Sensor

| Sensor Pin | ESP32 Connection |
|---|---|
| VCC | 3V3 |
| GND | GND |
| AO | GPIO34 |

Place the probe where leaked water is likely to collect first. Keep the sensor module's circuit board dry.

### LED and Buzzer

| Component Pin | ESP32 Connection |
|---|---|
| GPIO26 | 220 Ω resistor, then LED long leg |
| LED short leg | GND |
| Buzzer positive (+) | GPIO25 |
| Buzzer negative (−) | GND |

The LED resistor must remain in series with the LED. If the buzzer requires more current than an ESP32 GPIO can safely supply, use a suitable transistor driver.

### TFT Display — 1.8-inch SPI, 128 × 160

| TFT Pin | ESP32 Connection |
|---|---|
| VCC | 3V3 |
| GND | GND |
| LED / BL | 3V3 |
| SCK | GPIO18 |
| SDA / MOSI | GPIO23 |
| CS | GPIO4 |
| AO / A0 / DC | GPIO16 |
| RESET / RST | GPIO17 |

On this SPI display, SDA is the SPI data pin, not I²C SDA. Power the display from 3.3 V unless the exact module specifies otherwise.

## How It Works

1. The water-flow sensor generates pulses as water passes through it.
2. The ESP32 calculates the estimated flow rate.
3. The firmware accumulates water usage while flow is detected.
4. The analog leak sensor monitors its probe reading.
5. When the reading crosses the configured threshold, the system activates the leak warning.
6. The TFT displays the current readings while the ESP32 serves a web dashboard over Wi-Fi.

## Access the Web Dashboard

1. Upload the SmartFlow firmware to the ESP32.
2. Open Wi-Fi settings on your phone or computer.
3. Connect to the Wi-Fi network:

   **Network:** `SmartFlow-ESP32`

   **Password:** `SmartFlow123`

4. Open your browser and visit:

   **http://192.168.4.1**

The dashboard is hosted locally by the ESP32. A message saying "Connected without internet" is normal while using this network.

If the network does not appear, verify that the correct firmware is uploaded and that the ESP32 has restarted successfully.

## Leak Detection and Alerts

The configured leak threshold is **4000**. When the analog reading falls below this value, the system is intended to:

- Blink the red LED
- Activate the buzzer in a repeating warning pattern
- Show a leak alert on the web dashboard
- Display the leak status on the TFT

Actual sensor readings depend on the probe, moisture, and environment. Test both dry and wet conditions and calibrate the threshold before relying on the alerts.

## Water Usage and Cost Analysis

The web dashboard provides daily, weekly, and monthly usage views and lets you enter a water price in rupees per litre.

**Estimated water cost:**

`Cost (₹) = Water used (litres) × Price (₹ per litre)`

Example:

If 120 litres are used and the price is ₹0.50 per litre:

`120 × 0.50 = ₹60`

The total-water counter currently starts from zero when the ESP32 restarts. Do not assume the totals persist across restarts unless persistent storage is implemented and tested.

## Testing Procedure

1. Power the ESP32 through USB and verify all common ground connections.
2. Check that the TFT displays the flow rate, total usage, and leak status.
3. Run water through the flow sensor in the direction of its arrow.
4. Confirm that the pulse count and flow rate increase.
5. Test the leak probe with a damp cloth or a few drops of water on its exposed tracks.
6. Confirm that the LED, buzzer, TFT, and web alert respond.
7. Connect to the ESP32 Wi-Fi network and verify that the dashboard updates.

Keep water away from the ESP32, breadboard, USB connection, and sensor circuit boards.

## Project Structure

```text
SmartFlow/
├── README.md
├── smartflow.ino
└── images/
    ├── smartflow-tft.jpg
    ├── smartflow-hardware.jpg
    └── smartflow-dashboard.png
```

This is an example structure. Use the actual firmware filename in your repository and upload your own project photos and dashboard screenshots.

## Future Improvements

- Store water-use totals across restarts
- Improve flow-sensor calibration
- Add reliable time synchronization
- Export usage reports as CSV
- Add sensor-fault detection
- Build a protective enclosure for the electronics.

## Author

**Prateesraj K**  
Electronics and Communication Engineering  
Coimbatore Institute of Technology.
