# Dual Round Display Interactive Eye Simulation (ESP32-C3)

An ultra-responsive, 60 FPS multi-mode animated eye simulation designed for **two 1.28" GC9A01 round TFT displays** connected in parallel, driven by an **ESP32-C3**, and controlled via an **HW-040 Rotary Encoder**.

---

## Features

- **Minion Eye Mode (No Yellow)**:
  - Metallic silver/gray circular goggle rim.
  - Pure clean white sclera (no yellow skin).
  - Warm chestnut/honey-brown iris with radial spokes, black pupil, and white specular catch-light.
  - Autonomous organic gaze movement with minimum-jerk saccades, involuntary micro-jitter, and snappy **70 ms cartoon blinks**.
- **Cute Anime Eye Mode**:
  - Giant glossy black pupil with high-contrast catch-lights.
  - Supports **Style 1 (Triple Glint)** and **Style 2 (Twin Glint)**.
  - Real-time interactive gaze steering via the rotary encoder knob.
  - Autonomous idle breathing and cute anime smile blinks when left untouched.
- **Animated Sleep Mode**:
  - Exact cartoon sleeping eye with **5 downward-radiating eyelashes**.
  - **Animated floating "Z z z"**: 3 cartoon Z's emerge continuously from the eye, float upward along an organic S-curve sway, grow in size, and smoothly fade into the white background.
  - Directional wake-up: turning the knob right wakes up into Cute Eye; turning left wakes up into Minion Eye!
- **High Performance (60+ FPS)**:
  - Custom integer scanline engine specifically optimized for the ESP32-C3 RISC-V core (zero per-pixel float/trig math).
  - Band-aligned SPI DMA transfers with row clipping.

---

## Hardware Bill of Materials (BOM)

| Component | Description | Quantity |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32-C3 Development Board (SuperMini, DevKitM-1, NodeMCU-C3, etc.) | 1 |
| **Displays** | 1.28" Round TFT LCD Display (GC9A01 driver, 240×240, SPI interface) | 2 |
| **Input** | HW-040 (or KY-040) Rotary Encoder Module with Pushbutton | 1 |
| **Wiring** | Breadboard and jumper wires (or custom solder harness) | 1 set |
| **Power/Data** | USB Type-C or Micro-USB cable | 1 |

---

## Wiring & Pinout Connections

### 1. Dual GC9A01 Displays $\rightarrow$ ESP32-C3 (Parallel Connection)

Both displays share the **exact same SPI pins** on the ESP32-C3. When the microcontroller transmits pixel data, both screens receive the signal simultaneously, ensuring 100% synchronized stereoscopic movement with zero latency and zero extra memory.

```
                  +---------------------------+
                  |         ESP32-C3          |
                  +---------------------------+
                     |   |   |   |   |   |   |
    +----------------+   |   |   |   |   |   +----------------+
    | 3.3V  GND  GPIO4 GPIO6 GPIO7 GPIO2 GPIO10  GND   3.3V       |
    |  |     |     |     |     |     |     |      |      |        |
    | VCC   GND   SCL   SDA   DC    RST   CS     BLK    ---       |
    |  (Display 1 - Left Eye)                             |       |
    |                                                     |       |
    | VCC   GND   SCL   SDA   DC    RST   CS     BLK    --+       |
    |  (Display 2 - Right Eye)                                    |
    +-------------------------------------------------------------+
```

| GC9A01 Display Pin | ESP32-C3 Pin | Function | Notes |
| :--- | :--- | :--- | :--- |
| **VCC** | **3.3V** | Power Supply | Both display VCC pins connect to 3.3V |
| **GND** | **GND** | Power Ground | Both display GND pins connect to GND |
| **SCL / SCK** | **GPIO 4** | SPI Clock | Shared clock line |
| **SDA / MOSI** | **GPIO 6** | SPI Master Out Slave In | Shared data line |
| **DC** | **GPIO 7** | Data / Command Select | Shared D/C line |
| **RST** | **GPIO 2** | Hardware Reset | Shared reset line |
| **CS** | **GPIO 10** | Chip Select | Shared chip select (tied together) |
| **BLK** | **3.3V** | Backlight Enable | Tie to 3.3V (or a GPIO for PWM dimming) |

> [!IMPORTANT]
> **Power Supply Tip**: Each GC9A01 display backlight consumes approximately 40–50 mA (80–100 mA for both displays). Ensure your ESP32-C3 board's on-board 3.3V regulator can supply at least 300 mA of clean 3.3V power.

---

### 2. HW-040 Rotary Encoder $\rightarrow$ ESP32-C3

| HW-040 Pin | ESP32-C3 Pin | Function | Notes |
| :--- | :--- | :--- | :--- |
| **CLK / A** | **GPIO 0** | Encoder Phase A | Hardware interrupt line |
| **DT / B** | **GPIO 1** | Encoder Phase B | Direction sensing line |
| **SW** | **GPIO 5** | Pushbutton Switch | Active LOW (internal pull-up enabled) |
| **+ / VCC** | **3.3V** | Pull-Up Supply | Powers the module's 10k pull-up resistors |
| **GND** | **GND** | Ground | Ground connection |

> [!TIP]
> **Alternate Sleep Button**: The ESP32-C3's built-in **BOOT button (GPIO 9)** is also mapped as a secondary Sleep/Wake button. You can press the physical BOOT button on the board at any time to toggle Sleep mode!

---

## Software Setup & Upload Guide

### Prerequisites

1. **Install Arduino IDE**: Download and install [Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. **Install ESP32 Board Package**:
   - Open Arduino IDE $\rightarrow$ **File** $\rightarrow$ **Preferences**.
   - In *Additional Boards Manager URLs*, add:
     ```text
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Open **Tools** $\rightarrow$ **Board** $\rightarrow$ **Boards Manager**, search for `esp32` by **Espressif Systems**, and click **Install**.
3. **Install Required Library**:
   - Open **Sketch** $\rightarrow$ **Include Library** $\rightarrow$ **Manage Libraries...**
   - Search for **`GFX Library for Arduino`** (by *Moon On Our Nation*).
   - Click **Install**.

---

### Arduino IDE Board Settings

In the **Tools** menu, select the following settings for your ESP32-C3 board:

| Setting | Recommended Value |
| :--- | :--- |
| **Board** | `ESP32C3 Dev Module` (or your specific C3 board) |
| **CPU Frequency** | `160MHz (WiFi)` |
| **Flash Frequency** | `80MHz` |
| **Flash Mode** | `QIO` (or `DIO`) |
| **Flash Size** | `4MB (32Mb)` |
| **Partition Scheme** | `Default 4MB with spiffs` (or `Huge APP`) |
| **Core Debug Level** | `None` (or `Info` for serial logging) |
| **USB CDC On Boot** | `Enabled` *(Important for boards with native USB like SuperMini)* |
| **Upload Speed** | `921600` (or `460800` if upload fails) |
| **Port** | Select the COM port corresponding to your ESP32-C3 |

---

### Step-by-Step Upload Instructions

1. Clone or download this repository.
2. Ensure the folder is named `RealisticEye` and contains:
   - `RealisticEye.ino`
   - `cartoon_eye.h`
   - `sleep_eye.h`
3. Open `RealisticEye.ino` in Arduino IDE.
4. Connect your ESP32-C3 to your computer using a quality USB data cable.
5. Select your COM Port under **Tools** $\rightarrow$ **Port**.
6. Click the **Upload** button ($\rightarrow$).
7. Once flashing completes, open the **Serial Monitor** at **115200 baud**. You will see:
   ```text
   ==========================================
    Interactive 60 FPS Eye Simulation Active 
    - Turn RIGHT : CUTE EYE                  
    - Turn LEFT  : MINION EYE (No Yellow)   
    - Press SW   : SLEEP MODE (Z z z)        
    - Turn either: WAKE UP in that direction 
   ==========================================
   ```

---

### Troubleshooting

- **Upload hangs at "Connecting..."**:
  - Hold down the **BOOT** button on the ESP32-C3.
  - Press and release the **RESET** button.
  - Release the **BOOT** button (the board is now in bootloader download mode).
  - Click **Upload** again in Arduino IDE.
- **Display is blank / white screen**:
  - Double check wiring on GPIO 4 (SCL), GPIO 6 (SDA), GPIO 7 (DC), GPIO 2 (RST), and GPIO 10 (CS).
  - Verify that the BLK pin is connected to 3.3V.
- **Rotation direction is reversed**:
  - In `RealisticEye.ino`, change `#define ENCODER_REVERSE false` to `#define ENCODER_REVERSE true`.

---

## Operating Instructions & Gesture Controls

```text
       [ ROTARY ENCODER CONTROLS ]

           Turn Counter-Clockwise (LEFT)
                      <---
              [ MINION EYE MODE ]
           (Pure white sclera, hazel
           iris, autonomous saccades)
                      |
                      |  Press Button (SW or BOOT)
                      v
             [ SLEEP MODE (Zzz) ]
           (Closed eyelid + 5 lashes
            + floating cartoon Z's)
                      ^
                      |  Turn Knob (Either direction)
                      |  (WAKES UP in that direction!)
              [ CUTE EYE MODE ]
           (Giant glossy anime eye,
           interactive gaze steering)
                      --->
              Turn Clockwise (RIGHT)
```

1. **Turn Knob to the RIGHT**:
   - Immediately switches to **Cute Eye mode**.
   - If asleep $\rightarrow$ **wakes up** into Cute Eye mode!
   - Steers the cute gaze horizontally to follow the knob.
2. **Turn Knob to the LEFT**:
   - Immediately switches to **Minion Eye mode** (pure white sclera, no yellow).
   - If asleep $\rightarrow$ **wakes up** into Minion Eye mode!
   - Steers gaze leftward, then resumes natural autonomous looking around.
3. **Press the Encoder Button (SW on GPIO 5 or BOOT on GPIO 9)**:
   - Immediately puts the eye into **Sleep Mode**.
   - Displays the resting eyelid with 5 eyelashes and animated floating "Z z z"s.
   - Pressing again wakes up to Minion Eye.
4. **Long-Press the Button (> 1 sec)**:
   - In Cute Eye mode, cycles between **Style 1 (Triple Glint)** and **Style 2 (Twin Glint)**.

