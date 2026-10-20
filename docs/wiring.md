# EdgeECG — Hardware Pinout & Wiring Guide

This guide details the physical electrical connections between the **ESP32 Development Board** and the **AD8232 Heart Rate Monitor Analog Front End (AFE)**.

---

## 1. Pin Mapping Table

| AD8232 Pin | Function | ESP32 Pin | Notes |
|:---|:---|:---|:---|
| **3.3V** | Power Supply Input | **3V3** | Connect to ESP32 3.3V regulated rail. Do **NOT** connect to 5V (AD8232 max rating: 3.6V). |
| **GND** | Ground Reference | **GND** | Common system ground. |
| **OUTPUT** | Analog ECG Signal | **GPIO 34** (ADC1_CH6) | Input-only GPIO. Recommended attenuation: `ADC_ATTEN_DB_12` (0–3.3V full-scale). |
| **LO+** | Leads-Off Positive | **GPIO 18** | Digital input with internal pull-down. High indicates lead disconnected. |
| **LO-** | Leads-Off Negative | **GPIO 19** | Digital input with internal pull-down. High indicates lead disconnected. |
| **SDN** | Shutdown Pin | **GPIO 23** (or 3V3) | Active low shutdown. Tie to High (3.3V) for normal operation, or control via GPIO. |

> [!NOTE]
> GPIO 34 belongs to **ADC1**. Always use ADC1 pins (GPIO 32–39) on ESP32 because **ADC2** cannot be read while Wi-Fi is active.

---

## 2. Lead Placement (Einthoven's Triangle / 3-Lead)

Standard 3-lead placement using color-coded snap cables:

1. **RA (Right Arm / Red or White)**:
   - Placement: Right side, directly beneath the right clavicle (collarbone).
2. **LA (Left Arm / Yellow or Black)**:
   - Placement: Left side, directly beneath the left clavicle.
3. **RL (Right Leg / Green / Reference)**:
   - Placement: Lower right abdomen or right lower ribcage (serves as Driven Right Leg / ground reference).

---

## 3. Electrical Characteristics & Verification

- **Analog Output Range**: Typically centered at virtual ground $V_{REF} \approx \frac{V_{DD}}{2} = 1.65\text{ V}$.
- **Peak Signal Amplitude**: Under normal skin contact, peak QRS amplitude ranges between 200 mV and 1.5 V pk-pk centered at 1.65 V.
- **ESP32 ADC Attenuation**:
  - `ADC_ATTEN_DB_12` (formerly `ADC_ATTEN_DB_11`) allows readings up to approximately 3.1V - 3.3V.
  - Saturated readings ($ADC \ge 4090$ or $ADC \le 10$) trigger the `acq_is_saturated()` alert flag in firmware.
