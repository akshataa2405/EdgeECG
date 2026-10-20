# EdgeECG — Algorithm Validation Protocol & Benchmark

This document details the evaluation methodology, dataset partitions, and metric definitions used to benchmark the EdgeECG QRS detection algorithm against the gold-standard **MIT-BIH Arrhythmia Database**.

---

## 1. Dataset & Partitions

The MIT-BIH Arrhythmia Database contains 48 half-hour two-channel ambulatory ECG recordings sampled at 360 Hz.

### 1.1 Dataset Split Protocol
To prevent data snooping and tuning bias, records are split into two disjoint subsets prior to any parameter optimization:

- **Development Set (32 records)**: Used for algorithmic tuning, threshold setting, and filter coefficient verification.
  `100, 101, 103, 105, 106, 108, 109, 111, 112, 113, 114, 115, 116, 117, 118, 119, 121, 122, 123, 124, 200, 201, 202, 203, 205, 207, 208, 209, 210, 212, 213, 214`
- **Held-Out Evaluation Set (12 records)**: Evaluated only once without parameter adjustment.
  `215, 217, 219, 220, 221, 222, 223, 228, 230, 231, 232, 233, 234`
- **Excluded Records (4 records)**:
  `102, 104, 107, 217 (Channel 2 only / paced)`: Excluded due to ventricular pacemaker rhythms with high-amplitude artificial stimulus pulses, per ANSI/AAMI EC57 recommendations.

---

## 2. Resampling & Annotation Alignment

- **Sampling Rate Conversion**: MIT-BIH recordings (360 Hz) are resampled to the EdgeECG firmware target rate of 250 Hz using polyphase FIR resampling (`scipy.signal.resample_poly`) with proper anti-aliasing low-pass filtering.
- **Timestamp Mapping**:
  $$\text{Sample}_{250} = \text{round}\left(\text{Sample}_{360} \times \frac{250}{360}\right)$$
- **Reference Annotations**: Only QRS beat annotations (normal beats `N`, bundle branch blocks `L`, `R`, ventricular ectopic beats `V`, etc.) are included as reference targets; non-beat annotations (rhythm changes, artifact flags) are excluded.

---

## 3. Matching & Performance Metrics

### 3.1 Greedy One-to-One Matching
For each reference annotation beat timestamp $T_{\text{ref}}$, the closest unassigned detected peak timestamp $T_{\text{det}}$ is matched:
- If $|T_{\text{det}} - T_{\text{ref}}| \le \Delta t_{\text{tol}}$, it is classified as a **True Positive (TP)**.
- Any reference beat without a matching detection within $\Delta t_{\text{tol}}$ is a **False Negative (FN)**.
- Any detected peak without a reference beat within $\Delta t_{\text{tol}}$ is a **False Positive (FP)**.
- **Tolerance**: Primary evaluation at $\Delta t_{\text{tol}} = 100\text{ ms}$; sensitivity analysis conducted at $\Delta t_{\text{tol}} = 50\text{ ms}$.

### 3.2 Evaluation Metrics
$$\text{Sensitivity (Recall)} = \frac{TP}{TP + FN}$$
$$\text{Positive Predictive Value (PPV / Precision)} = \frac{TP}{TP + FP}$$
$$F_1\text{ Score} = 2 \times \frac{\text{Sensitivity} \times \text{PPV}}{\text{Sensitivity} + \text{PPV}} = \frac{2 \cdot TP}{2 \cdot TP + FP + FN}$$
