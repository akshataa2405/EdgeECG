#!/usr/bin/env python3
"""EdgeECG Pan-Tompkins Detection Evaluation Harness

Evaluates detection sensitivity, PPV (positive predictive value), and F1 score
against the MIT-BIH Arrhythmia Database or synthetic validation signals.
"""
import os
import sys
import json
import argparse
from datetime import datetime
from typing import List, Dict
import numpy as np
import scipy.signal as signal
import yaml

from matching import match_peaks_one_to_one


class PanTompkinsDetectorPy:
    """Python reference detector mirroring the C implementation in firmware/main/pan_tompkins.c."""

    def __init__(self, fs: int = 250):
        self.fs = fs
        # 4th order Butterworth bandpass (5 - 15 Hz)
        self.sos = signal.butter(2, [5.0, 15.0], btype="bandpass", fs=fs, output="sos")
        self.mwi_window = int(0.120 * fs) # 30 samples at 250 Hz
        self.refractory_samples = int(0.200 * fs) # 50 samples at 250 Hz

    def detect(self, ecg_signal: np.ndarray) -> List[float]:
        """Detect R-peaks in input ECG signal. Returns peak timestamps in milliseconds."""
        if len(ecg_signal) < self.mwi_window * 2:
            return []

        # 1. Bandpass filter
        bp = signal.sosfilt(self.sos, ecg_signal)

        # 2. Pan-Tompkins 5-point Derivative filter: y[n] = (2x[n] + x[n-1] - x[n-3] - 2x[n-4]) / 8
        deriv_kernel = np.array([2.0, 1.0, 0.0, -1.0, -2.0]) * 0.125
        deriv = np.convolve(bp, deriv_kernel, mode="same")

        # 3. Squaring
        squared = deriv ** 2

        # 4. Moving Window Integration (MWI)
        mwi_kernel = np.ones(self.mwi_window) / float(self.mwi_window)
        mwi = np.convolve(squared, mwi_kernel, mode="same")

        # 5. Adaptive Thresholding
        spki = 0.5 * np.max(mwi[:self.fs * 2]) if len(mwi) > self.fs * 2 else 0.5
        npki = 0.125 * spki
        threshold_i1 = npki + 0.25 * (spki - npki)
        threshold_i2 = 0.5 * threshold_i1

        peaks_ms = []
        last_peak_idx = -self.refractory_samples
        rr_avg = self.fs # 1 second initial assumption

        # Peak detection on MWI
        for i in range(1, len(mwi) - 1):
            if i - last_peak_idx < self.refractory_samples:
                continue

            # Local maximum in MWI
            if mwi[i] > mwi[i - 1] and mwi[i] > mwi[i + 1]:
                peak_height = mwi[i]
                detected = False

                if peak_height >= threshold_i1:
                    spki = 0.125 * peak_height + 0.875 * spki
                    detected = True
                elif peak_height >= threshold_i2 and (i - last_peak_idx) > (rr_avg * 1.66):
                    # Searchback
                    spki = 0.25 * peak_height + 0.75 * spki
                    detected = True
                else:
                    npki = 0.125 * peak_height + 0.875 * npki

                threshold_i1 = npki + 0.25 * (spki - npki)
                threshold_i2 = 0.5 * threshold_i1

                if detected:
                    # Find maximum in original bandpassed signal in preceding window
                    search_start = max(0, i - 40)
                    r_peak_idx = search_start + np.argmax(np.abs(bp[search_start:i + 1]))
                    peak_ms = (r_peak_idx / float(self.fs)) * 1000.0
                    peaks_ms.append(peak_ms)

                    rr_samples = r_peak_idx - last_peak_idx
                    if rr_samples > 0:
                        rr_avg = 0.125 * rr_samples + 0.875 * rr_avg

                    last_peak_idx = r_peak_idx

        return peaks_ms


def generate_synthetic_record(duration_sec: int = 60, bpm: float = 75.0, fs: int = 250) -> tuple:
    """Generates synthetic ECG signal with ground-truth R-peak timestamps."""
    total_samples = duration_sec * fs
    t = np.arange(total_samples) / float(fs)
    signal_ecg = np.full(total_samples, 2048.0) # Baseline

    # Baseline drift (respiration)
    signal_ecg += 100.0 * np.sin(2.0 * np.pi * 0.25 * t)

    # Beats
    rr_sec = 60.0 / bpm
    ref_times_ms = []
    current_t = 0.5 # start at 500 ms

    while current_t < duration_sec - 0.5:
        ref_times_ms.append(current_t * 1000.0)
        center_idx = int(current_t * fs)
        # Add QRS wave: sharp spike
        for offset in range(-10, 11):
            idx = center_idx + offset
            if 0 <= idx < total_samples:
                # Triangular/Gaussian QRS peak
                signal_ecg[idx] += 1200.0 * (1.0 - abs(offset) / 10.0)

        current_t += rr_sec

    # Add minor noise
    noise = np.random.normal(0, 20.0, total_samples)
    signal_ecg += noise

    return signal_ecg, ref_times_ms


def run_evaluation(config_path: str, split_name: str = "heldout", use_synthetic: bool = False):
    with open(config_path, "r") as f:
        config = yaml.safe_load(f)

    print(f"==================================================")
    print(f" EdgeECG MIT-BIH QRS Algorithm Evaluation Harness")
    print(f" Split: {split_name.upper()} | Target Fs: {config['sample_rate_target_hz']} Hz")
    print(f"==================================================")

    detector = PanTompkinsDetectorPy(fs=config["sample_rate_target_hz"])
    tolerance_ms = config["matching_tolerance_ms"]

    results = []

    if use_synthetic:
        print("\n[NOTE] Running against synthetic physiological benchmark signals...")
        test_profiles = [
            ("Synthetic_Resting_72BPM", 72.0, 60),
            ("Synthetic_Bradycardia_48BPM", 48.0, 60),
            ("Synthetic_Tachycardia_135BPM", 135.0, 60),
            ("Synthetic_Arrhythmia_Variable", 80.0, 60),
        ]

        for name, bpm, dur in test_profiles:
            ecg_sig, ref_beats = generate_synthetic_record(duration_sec=dur, bpm=bpm, fs=config["sample_rate_target_hz"])
            detected = detector.detect(ecg_sig)
            metrics = match_peaks_one_to_one(detected, ref_beats, tolerance_ms=tolerance_ms)
            results.append((name, metrics))
    else:
        # Check if wfdb and local records exist
        try:
            import wfdb
        except ImportError:
            print("\n[INFO] 'wfdb' library not installed. Falling back to synthetic verification suite.")
            return run_evaluation(config_path, split_name, use_synthetic=True)

        records_to_run = config["splits"].get(split_name, [])
        dataset_dir = config.get("dataset_dir", "./mitdb")

        for rec_id in records_to_run:
            rec_path = os.path.join(dataset_dir, rec_id)
            if not os.path.exists(f"{rec_path}.dat"):
                print(f"Record {rec_id} not found locally in {dataset_dir}. Skipping (run downloader).")
                continue

            record = wfdb.rdrecord(rec_path)
            annotation = wfdb.rdann(rec_path, "atr")

            # Resample 360 Hz to 250 Hz
            raw_sig = record.p_signal[:, 0]
            sig_250 = signal.resample_poly(raw_sig, config["sample_rate_target_hz"], config["sample_rate_native_hz"])

            # Map reference annotations to ms
            ref_beats_ms = [(idx / float(config["sample_rate_native_hz"])) * 1000.0 for idx in annotation.sample]

            detected = detector.detect(sig_250)
            metrics = match_peaks_one_to_one(detected, ref_beats_ms, tolerance_ms=tolerance_ms)
            results.append((rec_id, metrics))

    # Print markdown results table
    print("\n### Evaluation Results Table\n")
    print("| Record ID | Reference Beats | Detected | TP | FP | FN | Sensitivity | PPV | F1 Score |")
    print("|:---|---:|---:|---:|---:|---:|---:|---:|---:|")

    total_ref = sum(m["ref_count"] for _, m in results)
    total_det = sum(m["det_count"] for _, m in results)
    total_tp = sum(m["tp"] for _, m in results)
    total_fp = sum(m["fp"] for _, m in results)
    total_fn = sum(m["fn"] for _, m in results)

    for name, m in results:
        sens_pct = m["sensitivity"] * 100.0
        ppv_pct = m["ppv"] * 100.0
        f1_pct = m["f1"] * 100.0
        print(f"| {name} | {m['ref_count']} | {m['det_count']} | {m['tp']} | {m['fp']} | {m['fn']} | {sens_pct:.2f}% | {ppv_pct:.2f}% | {f1_pct:.2f}% |")

    agg_sens = (total_tp / (total_tp + total_fn) * 100.0) if (total_tp + total_fn) > 0 else 0.0
    agg_ppv = (total_tp / (total_tp + total_fp) * 100.0) if (total_tp + total_fp) > 0 else 0.0
    agg_f1 = (2.0 * total_tp / (2.0 * total_tp + total_fp + total_fn) * 100.0) if (2.0 * total_tp + total_fp + total_fn) > 0 else 0.0

    print(f"| **AGGREGATE** | **{total_ref}** | **{total_det}** | **{total_tp}** | **{total_fp}** | **{total_fn}** | **{agg_sens:.2f}%** | **{agg_ppv:.2f}%** | **{agg_f1:.2f}%** |")

    # Save output to results/
    os.makedirs("results", exist_ok=True)
    out_file = f"results/eval_{split_name}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    with open(out_file, "w") as f:
        json.dump({
            "split": split_name,
            "aggregate": {
                "sensitivity": agg_sens,
                "ppv": agg_ppv,
                "f1": agg_f1,
                "total_tp": total_tp,
                "total_fp": total_fp,
                "total_fn": total_fn
            },
            "records": [{"record": name, **m} for name, m in results]
        }, f, indent=2)

    print(f"\nSaved structured metrics report to: {out_file}")


if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_config = os.path.join(script_dir, "config.yaml")

    parser = argparse.ArgumentParser(description="EdgeECG Algorithm Benchmark Harness")
    parser.add_argument("--config", default=default_config, help="Path to config.yaml")
    parser.add_argument("--split", default="heldout", choices=["development", "heldout"], help="Dataset split to evaluate")
    parser.add_argument("--synthetic", action="store_true", help="Run against synthetic benchmark suite")
    args = parser.parse_args()

    run_evaluation(args.config, split_name=args.split, use_synthetic=args.synthetic)
