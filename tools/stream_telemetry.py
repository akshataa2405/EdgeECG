#!/usr/bin/env python3
"""EdgeECG Telemetry Stream Simulator

Simulates an ESP32 sending real-time ECG telemetry batches to the local backend.
Generates realistic physiological heart rates, HRV RMSSD variations, and periodic flags.
"""
import time
import math
import random
import requests
import argparse

BACKEND_URL = "http://127.0.0.1:8000/api/v1/ingest"
DEVICE_KEY = "edgeecg_device_secret_sample_key"
DEVICE_ID = "edgeecg-001"


def get_next_sequence(device_id: str) -> int:
    try:
        resp = requests.get(f"http://127.0.0.1:8000/api/v1/devices/{device_id}/latest", timeout=2.0)
        if resp.status_code == 200:
            data = resp.json()
            if data.get("latest_record") and data["latest_record"].get("sequence"):
                return data["latest_record"]["sequence"] + 1
    except Exception:
        pass
    return 1


def reset_device_history(device_id: str):
    try:
        resp = requests.delete(f"http://127.0.0.1:8000/api/v1/devices/{device_id}/records", timeout=2.0)
        if resp.status_code == 200:
            print(f">>> Device history reset for '{device_id}' (cleared old records).")
    except Exception as e:
        print(f">>> Note: Could not reset device records: {e}")


def run_streamer(interval_sec: float = 1.0, total_batches: int = 60, reset: bool = False, start_seq: int = None):
    if reset:
        reset_device_history(DEVICE_ID)

    if start_seq is not None:
        seq = start_seq
    elif reset:
        seq = 1
    else:
        seq = get_next_sequence(DEVICE_ID)

    print(f"==================================================")
    print(f" EdgeECG Telemetry Stream Simulator")
    print(f" Target: {BACKEND_URL} | Device: {DEVICE_ID}")
    print(f" Starting Sequence: #{seq} | Streaming {total_batches} batches at {interval_sec}s interval...")
    print(f"==================================================")

    session_id = f"sim-session-{random.randint(1000, 9999)}"
    base_hr = 72.0
    detected_peaks = 0
    rejected_peaks = 0

    rr_history = []

    for b in range(1, total_batches + 1):
        # Realistic sinus arrhythmia breathing modulation (~0.25 Hz)
        t = b * interval_sec
        hr_variation = 6.0 * math.sin(2.0 * math.pi * 0.25 * t) + random.uniform(-1.5, 1.5)

        # Inject an occasional ectopic jump or tachycardia spike for demonstration
        if b == 15:
            current_hr = 132.0 # Tachycardia flag
            print(">>> [DEMO] Injecting transient tachycardia spike (132 BPM)")
        elif b == 30:
            current_hr = 46.0  # Bradycardia flag
            print(">>> [DEMO] Injecting transient bradycardia dip (46 BPM)")
        else:
            current_hr = round(base_hr + hr_variation, 1)

        rr_ms = round(60000.0 / current_hr, 1)
        rr_history.append(rr_ms)
        if len(rr_history) > 30:
            rr_history.pop(0)

        # Calculate RMSSD
        if len(rr_history) >= 10:
            diffs = [rr_history[i] - rr_history[i - 1] for i in range(1, len(rr_history))]
            rmssd_ms = round(math.sqrt(sum(d * d for d in diffs) / len(diffs)), 1)
        else:
            rmssd_ms = None

        detected_peaks += random.choice([1, 2])
        if random.random() < 0.05:
            rejected_peaks += 1

        # Synthesize actual raw AD8232 ECG voltage samples [mV] matching this beat
        num_samples = max(20, int(interval_sec * 50))
        batch_raw = []
        rr_sec = max(0.2, rr_ms / 1000.0)
        for s in range(num_samples):
            sample_time = t + s * (interval_sec / num_samples)
            p = (sample_time % rr_sec) / rr_sec
            v = random.uniform(-0.02, 0.02)
            v += 0.18 * math.exp(-pow((p - 0.16) / 0.04, 2))
            v -= 0.16 * math.exp(-pow((p - 0.28) / 0.015, 2))
            v += 1.35 * math.exp(-pow((p - 0.32) / 0.016, 2))
            v -= 0.42 * math.exp(-pow((p - 0.36) / 0.016, 2))
            v += 0.35 * math.exp(-pow((p - 0.55) / 0.065, 2))
            batch_raw.append(round(v, 3))

        payload = {
            "schema_version": 1,
            "device_id": DEVICE_ID,
            "records": [
                {
                    "sequence": seq,
                    "session_id": session_id,
                    "timestamp_ms": int(time.time() * 1000),
                    "heart_rate_bpm": current_hr,
                    "rmssd_ms": rmssd_ms,
                    "rr_interval_ms": rr_ms,
                    "signal_quality": "valid",
                    "detected_peaks": detected_peaks,
                    "rejected_peaks": rejected_peaks,
                    "raw_samples": batch_raw
                }
            ]
        }

        try:
            resp = requests.post(
                BACKEND_URL,
                headers={"X-Device-Key": DEVICE_KEY, "Content-Type": "application/json"},
                json=payload,
                timeout=3.0
            )
            if resp.status_code == 200:
                data = resp.json()
                print(f"[{b}/{total_batches}] Seq #{seq:03d} | HR: {current_hr:5.1f} BPM | RR: {rr_ms:5.1f} ms | RMSSD: {str(rmssd_ms):>5} ms | Ack: {data['accepted_sequences']}")
            else:
                print(f"[{b}/{total_batches}] HTTP {resp.status_code}: {resp.text}")
        except Exception as e:
            print(f"[{b}/{total_batches}] Connection error: {e}")

        seq += 1
        time.sleep(interval_sec)

    print("\nStream simulation completed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Stream simulated telemetry to EdgeECG backend")
    parser.add_argument("--interval", type=float, default=1.0, help="Interval in seconds between batches")
    parser.add_argument("--batches", type=int, default=35, help="Total batches to stream")
    parser.add_argument("--reset", action="store_true", help="Clear past database records and start sequence at #1")
    parser.add_argument("--seq", type=int, default=None, help="Explicit starting sequence number")
    args = parser.parse_args()

    run_streamer(interval_sec=args.interval, total_batches=args.batches, reset=args.reset, start_seq=args.seq)
