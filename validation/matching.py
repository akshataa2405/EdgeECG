"""Greedy One-to-One QRS Beat Matching Engine

Computes True Positives (TP), False Positives (FP), and False Negatives (FN)
between reference annotation beat times and detected peak timestamps according to ANSI/AAMI EC57.
"""
from typing import List, Tuple, Dict
import numpy as np


def match_peaks_one_to_one(
    detected_times_ms: List[float],
    reference_times_ms: List[float],
    tolerance_ms: float = 100.0
) -> Dict[str, float]:
    """
    Performs greedy one-to-one matching between detected peak times and reference beats.

    Parameters:
        detected_times_ms: List of detected R-peak timestamps in milliseconds.
        reference_times_ms: List of reference annotation beat timestamps in milliseconds.
        tolerance_ms: Coincidence matching window in milliseconds (default: 100 ms).

    Returns:
        Dictionary containing counts of TP, FP, FN, sensitivity, PPV, and F1 score.
    """
    if len(reference_times_ms) == 0:
        return {
            "ref_count": 0,
            "det_count": len(detected_times_ms),
            "tp": 0,
            "fp": len(detected_times_ms),
            "fn": 0,
            "sensitivity": 0.0,
            "ppv": 0.0,
            "f1": 0.0
        }

    det = np.sort(np.array(detected_times_ms, dtype=np.float64))
    ref = np.sort(np.array(reference_times_ms, dtype=np.float64))

    matched_det_indices = set()
    tp = 0
    fn = 0

    for r_time in ref:
        # Find all detections within window [r_time - tol, r_time + tol]
        candidates = np.where(
            (det >= r_time - tolerance_ms) &
            (det <= r_time + tolerance_ms)
        )[0]

        best_idx = None
        min_dist = float('inf')

        for c_idx in candidates:
            if c_idx not in matched_det_indices:
                dist = abs(det[c_idx] - r_time)
                if dist < min_dist:
                    min_dist = dist
                    best_idx = c_idx

        if best_idx is not None:
            matched_det_indices.add(best_idx)
            tp += 1
        else:
            fn += 1

    fp = len(det) - len(matched_det_indices)

    # Calculate metrics
    sensitivity = tp / (tp + fn) if (tp + fn) > 0 else 0.0
    ppv = tp / (tp + fp) if (tp + fp) > 0 else 0.0
    f1 = (2.0 * tp / (2.0 * tp + fp + fn)) if (2.0 * tp + fp + fn) > 0 else 0.0

    return {
        "ref_count": len(ref),
        "det_count": len(det),
        "tp": tp,
        "fp": fp,
        "fn": fn,
        "sensitivity": sensitivity,
        "ppv": ppv,
        "f1": f1
    }
