# AgriSense-Soil-Probe-Core

An industrial-grade soil telemetry engine designed for multi-electrode stainless steel probes. This core handles AC-excited electrical conductivity (EC) measurement, volumetric water content (VWC) characterization, and temperature-compensated parameter estimation.

---

## ⚙️ Hardware Specifications
* **Electrodes:** Multi-point Stainless Steel (Grade 314 / 316L)
* **Core MCU:** ESP32 / STM32
* **Excitation Scheme:** Bipolar AC square wave (prevents polarization and electroplating)
* **Communication Interface:** RS485 / Modbus RTU Slave
* **Temperature Tracking:** Integrated DS18B20 digital thermal baseline

---

## 🚀 Key Features
* **Zero-DC Bias Excitation:** Eliminates electrode degradation and galvanic drift during extended field deployments.
* **Temperature Compensation:** Real-time EC normalization mapped to standard 25°C reference values.
* **Industrial Protocol Support:** Direct integration into PLC, SCADA, and IoT gateway networks via Modbus holding registers.

---

## 📂 Repository Layout
```text
AgriSense-Soil-Probe-Core/
├── firmware/         # C/C++ firmware & signal acquisition routines
├── hardware/         # Schematics, simulation, and PCB design files
├── docs/             # Calibration curves and register maps
├── .gitignore
└── README.md