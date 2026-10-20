# EdgeECG — Safety Guidelines & Medical Disclaimer

> [!CAUTION]
> **NOT A MEDICAL DEVICE:** EdgeECG is an educational prototype and portfolio project designed to demonstrate embedded systems engineering, digital signal processing, offline synchronization, and backend systems. It has **NOT** been evaluated, certified, or cleared by the FDA, CE, CDSCO, or any medical regulatory authority.
> 
> **DO NOT** use EdgeECG for medical monitoring, clinical diagnosis, arrhythmia detection, therapy assessment, or any health-related decisions.

---

## 1. Electrical Safety

1. **Battery Power vs. Mains Isolation**:
   - Always power the prototype via battery or an electrically isolated laptop running on battery power when attaching electrodes.
   - **NEVER** connect electrodes to a human subject while the microcontroller is connected to a computer, debugger, or power supply that is plugged into an AC mains wall outlet.
   - Mains-powered USB ports lack medical-grade galvanic isolation (IEC 60601-1 requirement: 4 kV isolation, < 10 µA leakage current). A power supply fault or ground loop could deliver dangerous or lethal electric shocks through cardiac leads.

2. **Skin Contact & Electrodes**:
   - Use only standard, single-use, biocompatible Ag/AgCl ECG electrodes.
   - Do not use damaged cables, exposed bare wires, or makeshift conductive pads.
   - Stop use immediately if skin irritation, redness, or burning occurs.

3. **Bench Testing Protocol**:
   - During firmware development, algorithm tuning, and bench tests, use:
     - Synthetic test vectors generated in software,
     - Function generators with voltage dividers (< 3.3V pk-pk), or
     - Battery-powered patient simulators.
   - Do not test development firmware on living subjects.

---

## 2. Software & Algorithmic Disclaimer

- Heart rate (BPM) and Heart Rate Variability (RMSSD) calculations are software approximations based on standard algorithms (Pan-Tompkins).
- Detection flags (e.g., unusual RR intervals, tachycardia/bradycardia thresholds) are purely algorithmic rule checks and **do not** denote pathological conditions.
- Artifacts caused by electrode displacement, muscle tremors (EMG), respiration baseline wander, or powerline noise may cause false detections or missed beats.
