# Automated-Misting-System
An ESP32-based automated misting system with manual controls, timed scheduling, and AI-driven humidity optimization. Supports both manual control and scheduled misting, with a roadmap toward AI-driven environmental optimization.


##  Features
-  **Manual Misting Control**
    - Trigger misting instantly using onboard buttons

-  **Scheduled Misting (RTC-Based)**
    - Automatically mist at set times using a real-time clock module

-  **MOSFET-Controlled Pump**
    - Efficiently drives the pump from the ESP32

-  **Flyback Diode Protection**
    - Protects circuitry from voltage spikes caused by the pump motor

-  **Buck Converter Power Regulation**
    - Steps down voltage safely for ESP32 and peripherals

-  **OLED Display Interface**
    - Displays system status, time, and settings

-  **Button-Based UI**
    - Navigate and control system without external devices

---

##  Hardware Components

| Component        | Purpose |
|------------------|--------|
| ESP32            | Main microcontroller |
| MOSFET           | Controls pump power safely |
| Flyback Diode    | Protects against inductive voltage spikes |
| Buck Converter   | Steps down voltage for stable operation |
| RTC Module       | Enables accurate scheduling |
| OLED Display     | Displays system info |
| Push Buttons     | User input |
| Water Pump       | Generates mist |

---

##  How It Works

### Manual Mode
1. User presses a button  
2. ESP32 detects input  
3. MOSFET is activated  
4. Pump turns on and mist is produced for a set time 

### Scheduled Mode
1. RTC keeps track of time  
2. When scheduled time is reached ESP32 triggers MOSFET  
3. Pump runs automatically for set amount of time 

### Electrical Safety
- The **MOSFET** allows the ESP32 to control a higher-power device safely  
- The **flyback diode** prevents damage from voltage spikes when the pump shuts off  
- The **buck converter** ensures proper voltage for all components  
