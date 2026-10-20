/**
 * EdgeECG Telemetry Workbench — Application Controller
 * High-precision physiological telemetry acquisition, stream sync, and event audit.
 */

// Configuration
const CONFIG = {
  apiBaseUrl: window.location.origin,
  pollIntervalMs: 5000,
  staleThresholdMs: 30000,
  offlineThresholdMs: 60000,
  defaultDeviceId: 'edgeecg-001',
};

// Application State
const state = {
  deviceId: CONFIG.defaultDeviceId,
  timeWindowMs: 180000, // Default 3 minutes (Live horizon)
  lastRecordSeq: null,
  pollTimer: null,
  chart: null,
  isFetchingHistory: false,
};

// DOM Element References
const elements = {
  deviceSelect: document.getElementById('device-id-select'),
  connectionBadge: document.getElementById('connection-badge'),
  connectionStatusText: document.getElementById('connection-status-text'),
  qualityBadge: document.getElementById('quality-badge'),
  qualityStatusText: document.getElementById('quality-status-text'),
  refreshBtn: document.getElementById('refresh-btn'),
  hrValue: document.getElementById('metric-hr'),
  hrPulse: document.getElementById('hr-pulse'),
  hrStatusHint: document.getElementById('hr-status-hint'),
  rmssdValue: document.getElementById('metric-rmssd'),
  rmssdStatusHint: document.getElementById('rmssd-status-hint'),
  rrValue: document.getElementById('metric-rr'),
  rrJumpBadge: document.getElementById('rr-jump-badge'),
  lastSyncAge: document.getElementById('metric-last-sync-age'),
  sequenceVal: document.getElementById('metric-sequence'),
  peaksRatio: document.getElementById('metric-peaks-ratio'),
  deviceSyncState: document.getElementById('device-sync-state'),
  timeWindowButtons: document.querySelectorAll('#time-window-group .window-tab, #time-window-group .btn-tab'),
  clearRecordsBtn: document.getElementById('clear-records-btn'),
  chartCanvas: document.getElementById('telemetryChart'),
  chartLoader: document.getElementById('chart-loader'),
  chartEmptyState: document.getElementById('chart-empty-state'),
  flagsTbody: document.getElementById('flags-tbody'),
  clearFlagsBtn: document.getElementById('clear-flags-btn'),
};

/**
 * Initialize Chart.js Telemetry Chart
 */
function initChart() {
  const ctx = elements.chartCanvas.getContext('2d');

  state.chart = new Chart(ctx, {
    type: 'line',
    data: {
      datasets: [
        {
          label: 'Heart Rate (BPM)',
          data: [],
          borderColor: '#38bdf8',
          backgroundColor: 'rgba(56, 189, 248, 0.04)',
          borderWidth: 2,
          pointRadius: 3,
          pointHoverRadius: 6,
          pointBackgroundColor: '#38bdf8',
          pointBorderColor: '#0a0d13',
          pointBorderWidth: 1.5,
          tension: 0.15,
          fill: true,
          spanGaps: false,
          yAxisID: 'yHR',
        },
        {
          label: 'HRV RMSSD (ms)',
          data: [],
          borderColor: '#c084fc',
          backgroundColor: 'transparent',
          borderWidth: 1.75,
          borderDash: [5, 4],
          pointRadius: 3,
          pointHoverRadius: 6,
          pointBackgroundColor: '#c084fc',
          pointBorderColor: '#0a0d13',
          pointBorderWidth: 1.5,
          tension: 0.15,
          fill: false,
          spanGaps: false,
          yAxisID: 'yRMSSD',
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 200 },
      interaction: {
        mode: 'index',
        intersect: false,
      },
      plugins: {
        legend: {
          display: true,
          position: 'top',
          align: 'end',
          labels: {
            color: '#cbd5e1',
            font: { family: "'Inter', sans-serif", size: 11, weight: '500' },
            boxWidth: 16,
            boxHeight: 3,
            padding: 14,
            usePointStyle: false,
          }
        },
        tooltip: {
          backgroundColor: '#0d1117',
          titleColor: '#f8fafc',
          bodyColor: '#cbd5e1',
          borderColor: '#222b3b',
          borderWidth: 1,
          padding: 10,
          cornerRadius: 4,
          titleFont: { family: "'Inter', sans-serif", size: 11, weight: '600' },
          bodyFont: { family: "'JetBrains Mono', monospace", size: 11 },
          displayColors: true,
          boxWidth: 8,
          boxHeight: 8,
          boxPadding: 4,
          callbacks: {
            title: function(items) {
              if (!items.length) return '';
              const d = new Date(items[0].raw.x);
              return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit', fractionalSecondDigits: 3 });
            },
            label: function(item) {
              const val = item.raw.y !== null ? item.raw.y.toFixed(1) : '--';
              const unit = item.datasetIndex === 0 ? 'BPM' : 'ms';
              return ` ${item.dataset.label}: ${val} ${unit}`;
            }
          }
        }
      },
      scales: {
        x: {
          type: 'linear',
          title: { display: false },
          grid: { color: 'rgba(255, 255, 255, 0.04)', lineWidth: 1 },
          ticks: {
            color: '#64748b',
            font: { family: "'JetBrains Mono', monospace", size: 10 },
            callback: function(val) {
              const d = new Date(val);
              return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
            }
          }
        },
        yHR: {
          type: 'linear',
          position: 'left',
          title: {
            display: true,
            text: 'Heart Rate [BPM]',
            color: '#38bdf8',
            font: { family: "'Inter', sans-serif", size: 10, weight: '600' },
            padding: { bottom: 6 }
          },
          min: 30,
          max: 180,
          grid: { color: 'rgba(255, 255, 255, 0.04)', lineWidth: 1 },
          ticks: {
            color: '#64748b',
            font: { family: "'JetBrains Mono', monospace", size: 10 },
            stepSize: 30
          }
        },
        yRMSSD: {
          type: 'linear',
          position: 'right',
          title: {
            display: true,
            text: 'RMSSD [ms]',
            color: '#c084fc',
            font: { family: "'Inter', sans-serif", size: 10, weight: '600' },
            padding: { bottom: 6 }
          },
          min: 0,
          suggestedMax: 200,
          grid: { drawOnChartArea: false },
          ticks: {
            color: '#64748b',
            font: { family: "'JetBrains Mono', monospace", size: 10 },
            stepSize: 40
          }
        }
      }
    }
  });
}

/**
 * Update Connection & Quality Badges
 */
function updateConnectionStatus(lastSeenAt, signalQuality) {
  const now = Date.now();
  if (!lastSeenAt) {
    elements.connectionBadge.className = 'instrument-badge badge-offline';
    elements.connectionStatusText.textContent = 'Offline';
    elements.deviceSyncState.textContent = 'Standby';
    elements.lastSyncAge.textContent = 'Never';
    return;
  }

  const ageMs = now - lastSeenAt;
  const ageSec = Math.floor(ageMs / 1000);

  if (ageSec < 60) {
    elements.lastSyncAge.textContent = `${ageSec}s ago`;
  } else if (ageSec < 3600) {
    elements.lastSyncAge.textContent = `${Math.floor(ageSec / 60)}m ago`;
  } else {
    elements.lastSyncAge.textContent = `${Math.floor(ageSec / 3600)}h ago`;
  }

  if (ageMs <= CONFIG.staleThresholdMs) {
    elements.connectionBadge.className = 'instrument-badge badge-connected';
    elements.connectionStatusText.textContent = 'Connected';
    elements.deviceSyncState.textContent = 'Streaming';
  } else if (ageMs <= CONFIG.offlineThresholdMs) {
    elements.connectionBadge.className = 'instrument-badge badge-stale';
    elements.connectionStatusText.textContent = 'Stale';
    elements.deviceSyncState.textContent = 'Delayed';
  } else {
    elements.connectionBadge.className = 'instrument-badge badge-offline';
    elements.connectionStatusText.textContent = 'Offline';
    elements.deviceSyncState.textContent = 'Buffering';
  }

  // Update Signal Quality Badge
  if (signalQuality === 'valid') {
    elements.qualityBadge.className = 'instrument-badge badge-connected';
    elements.qualityStatusText.textContent = 'Optimal';
  } else if (signalQuality === 'degraded') {
    elements.qualityBadge.className = 'instrument-badge badge-stale';
    elements.qualityStatusText.textContent = 'Noisy';
  } else if (signalQuality === 'invalid') {
    elements.qualityBadge.className = 'instrument-badge badge-offline';
    elements.qualityStatusText.textContent = 'Lead Off';
  } else {
    elements.qualityBadge.className = 'instrument-badge badge-neutral';
    elements.qualityStatusText.textContent = 'No Signal';
  }
}

/**
 * Trigger R-Peak Strobe Indicator
 */
function triggerHeartPulse() {
  elements.hrPulse.classList.add('pulse-active');
  setTimeout(() => {
    elements.hrPulse.classList.remove('pulse-active');
  }, 120);
}

/**
 * Poll Latest Metrics
 */
async function fetchLatestMetrics() {
  try {
    const url = `${CONFIG.apiBaseUrl}/api/v1/devices/${state.deviceId}/latest`;
    const res = await fetch(url);
    if (!res.ok) {
      if (res.status === 404) {
        updateConnectionStatus(null, null);
      }
      return;
    }

    const data = await res.json();
    const rec = data.latest_record;

    updateConnectionStatus(data.last_seen_at, rec ? rec.signal_quality : null);

    if (rec) {
      // Feed live telemetry to real-time Lead-II ECG oscilloscope engine
      if (window.ecgEngine && rec.heart_rate_bpm !== null) {
        window.ecgEngine.updateMetrics(
          rec.heart_rate_bpm,
          rec.rr_interval_ms,
          rec.signal_quality,
          rec.raw_samples
        );
      }

      // Trigger strobe on sequence change
      if (state.lastRecordSeq !== rec.sequence) {
        triggerHeartPulse();
        state.lastRecordSeq = rec.sequence;
      }

      // Heart Rate
      if (rec.heart_rate_bpm !== null) {
        elements.hrValue.textContent = rec.heart_rate_bpm.toFixed(1);
        if (rec.heart_rate_bpm < 50) {
          elements.hrStatusHint.textContent = 'Bradycardia';
          elements.hrStatusHint.className = 'metric-state-text status-text-warning';
        } else if (rec.heart_rate_bpm > 120) {
          elements.hrStatusHint.textContent = 'Tachycardia';
          elements.hrStatusHint.className = 'metric-state-text status-text-critical';
        } else {
          elements.hrStatusHint.textContent = 'Resting Nominal';
          elements.hrStatusHint.className = 'metric-state-text status-text-nominal';
        }
      } else {
        elements.hrValue.textContent = '--';
        elements.hrStatusHint.textContent = 'Invalid Signal';
        elements.hrStatusHint.className = 'metric-state-text status-text-neutral';
      }

      // RMSSD
      if (rec.rmssd_ms !== null) {
        elements.rmssdValue.textContent = rec.rmssd_ms.toFixed(1);
        elements.rmssdStatusHint.textContent = 'Stable Variance';
        elements.rmssdStatusHint.className = 'metric-state-text status-text-nominal';
      } else {
        elements.rmssdValue.textContent = '--';
        elements.rmssdStatusHint.textContent = 'Needs ≥ 10 Intervals';
        elements.rmssdStatusHint.className = 'metric-state-text status-text-neutral';
      }

      // RR Interval
      if (rec.rr_interval_ms !== null) {
        elements.rrValue.textContent = Math.round(rec.rr_interval_ms);
      } else {
        elements.rrValue.textContent = '--';
      }

      // RR Jump Chip
      if (rec.heart_rate_bpm && (rec.heart_rate_bpm > 120 || rec.heart_rate_bpm < 50)) {
        elements.rrJumpBadge.textContent = 'Deviation';
        elements.rrJumpBadge.className = 'state-chip chip-warning';
      } else {
        elements.rrJumpBadge.textContent = 'Nominal';
        elements.rrJumpBadge.className = 'state-chip chip-neutral';
      }

      // Sequence & Peak counters
      elements.sequenceVal.textContent = `#${rec.sequence}`;
      elements.peaksRatio.textContent = `${rec.detected_peaks} / ${rec.rejected_peaks}`;
    }
  } catch (err) {
    console.error('Error fetching latest metrics:', err);
  }
}

/**
 * Fetch Historical Telemetry for Trends Chart
 */
async function fetchHistory() {
  if (state.isFetchingHistory) return;
  state.isFetchingHistory = true;

  try {
    const url = `${CONFIG.apiBaseUrl}/api/v1/devices/${state.deviceId}/records?limit=500`;

    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);

    const data = await res.json();
    let records = data.records || [];

    if (records.length === 0) {
      elements.chartEmptyState.style.display = 'flex';
      state.chart.data.datasets[0].data = [];
      state.chart.data.datasets[1].data = [];
      state.chart.update();
      return;
    }

    elements.chartEmptyState.style.display = 'none';

    // Sort chronologically ascending
    records.sort((a, b) => a.timestamp_ms - b.timestamp_ms);

    // Filter records according to the active time horizon if applicable
    let activeRecords = records;
    const latestTs = records[records.length - 1].timestamp_ms;

    if (state.timeWindowMs !== 'all' && typeof state.timeWindowMs === 'number') {
      const windowStart = latestTs - state.timeWindowMs;
      activeRecords = records.filter(r => r.timestamp_ms >= windowStart);
      // Ensure we keep at least the last 50 points if window is tight
      if (activeRecords.length < 2) {
        activeRecords = records.slice(-50);
      }
    }

    const hrPoints = [];
    const rmssdPoints = [];
    const GAP_THRESHOLD_MS = 15000; // 15s gap breaks line continuity

    for (let i = 0; i < activeRecords.length; i++) {
      const r = activeRecords[i];
      if (i > 0) {
        const prev = activeRecords[i - 1];
        if (
          r.timestamp_ms - prev.timestamp_ms > GAP_THRESHOLD_MS ||
          (prev.session_id && r.session_id && r.session_id !== prev.session_id)
        ) {
          // Break line across gap
          hrPoints.push({ x: prev.timestamp_ms + 100, y: null });
          rmssdPoints.push({ x: prev.timestamp_ms + 100, y: null });
        }
      }

      if (r.heart_rate_bpm !== null) {
        hrPoints.push({ x: r.timestamp_ms, y: r.heart_rate_bpm });
      }
      if (r.rmssd_ms !== null) {
        rmssdPoints.push({ x: r.timestamp_ms, y: r.rmssd_ms });
      }
    }

    // Dynamic X-Axis Scaling:
    // If the data span is shorter than 70% of the window duration (e.g. streaming just started),
    // frame the active records so they fill the canvas instead of being squashed on the right edge!
    const firstTs = activeRecords[0].timestamp_ms;
    const activeSpanMs = latestTs - firstTs;

    if (state.timeWindowMs === 'all') {
      delete state.chart.options.scales.x.min;
      delete state.chart.options.scales.x.max;
    } else {
      if (activeSpanMs < state.timeWindowMs * 0.7) {
        const pad = Math.max(1000, Math.round(activeSpanMs * 0.08));
        state.chart.options.scales.x.min = firstTs - pad;
        state.chart.options.scales.x.max = latestTs + pad;
      } else {
        state.chart.options.scales.x.min = latestTs - state.timeWindowMs;
        state.chart.options.scales.x.max = latestTs + 1500;
      }
    }

    state.chart.data.datasets[0].data = hrPoints;
    state.chart.data.datasets[1].data = rmssdPoints;
    state.chart.update();
  } catch (err) {
    console.error('Error fetching history:', err);
  } finally {
    state.isFetchingHistory = false;
    elements.chartLoader.style.display = 'none';
  }
}

/**
 * Fetch Deviation Flags
 */
async function fetchFlags() {
  try {
    const url = `${CONFIG.apiBaseUrl}/api/v1/devices/${state.deviceId}/flags?limit=50`;
    const res = await fetch(url);
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }

    const data = await res.json();
    const flags = data.flags || [];

    if (flags.length === 0) {
      elements.flagsTbody.innerHTML = `
        <tr>
          <td colspan="6" class="table-empty">
            <div class="empty-inline-state">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                <circle cx="12" cy="12" r="10"></circle>
                <line x1="12" y1="8" x2="12" y2="12"></line>
                <line x1="12" y1="16" x2="12.01" y2="16"></line>
              </svg>
              <span>No physiological flags or RR interval deviations recorded in current session.</span>
            </div>
          </td>
        </tr>
      `;
      return;
    }

    elements.flagsTbody.innerHTML = flags.map(f => {
      const timeStr = new Date(f.timestamp_ms).toLocaleTimeString();
      const hrStr = f.heart_rate_bpm ? `${f.heart_rate_bpm.toFixed(1)} BPM` : '--';
      const rrStr = f.rr_interval_ms ? `${Math.round(f.rr_interval_ms)} ms` : '--';

      return `
        <tr>
          <td class="font-mono num-tabular">${timeStr}</td>
          <td class="font-mono num-tabular">#${f.sequence}</td>
          <td class="font-mono num-tabular" style="font-weight: 600; color: var(--text-primary);">${hrStr}</td>
          <td class="font-mono num-tabular">${rrStr}</td>
          <td><span class="flag-pill ${f.flag_type}">${f.flag_type.replace('_', ' ')}</span></td>
          <td style="color: var(--text-muted);">${f.description}</td>
        </tr>
      `;
    }).join('');
  } catch (err) {
    console.error('Error fetching flags:', err);
    elements.flagsTbody.innerHTML = `
      <tr>
        <td colspan="6" class="table-empty">
          <div class="empty-inline-state" style="color: var(--status-warning);">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true">
              <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"></path>
              <line x1="12" y1="9" x2="12" y2="13"></line>
              <line x1="12" y1="17" x2="12.01" y2="17"></line>
            </svg>
            <span>Failed to retrieve deviation audit log. Check backend connection.</span>
          </div>
        </td>
      </tr>
    `;
  }
}

/**
 * Setup Event Listeners
 */
function setupEventListeners() {
  // Time Window Tabs
  elements.timeWindowButtons.forEach(btn => {
    btn.addEventListener('click', () => {
      elements.timeWindowButtons.forEach(b => {
        b.classList.remove('active');
        b.setAttribute('aria-selected', 'false');
      });
      btn.classList.add('active');
      btn.setAttribute('aria-selected', 'true');
      const val = btn.dataset.window;
      state.timeWindowMs = val === 'all' ? 'all' : parseInt(val, 10);
      elements.chartLoader.style.display = 'flex';
      fetchHistory();
    });
  });

  // Clear / Reset Records Button
  if (elements.clearRecordsBtn) {
    elements.clearRecordsBtn.addEventListener('click', async () => {
      if (!confirm(`Confirm database reset for device '${state.deviceId}'? This action clears all telemetry records.`)) {
        return;
      }
      try {
        await fetch(`${CONFIG.apiBaseUrl}/api/v1/devices/${state.deviceId}/records`, {
          method: 'DELETE'
        });
        state.lastRecordSeq = null;
        fetchLatestMetrics();
        fetchHistory();
        fetchFlags();
      } catch (err) {
        console.error('Failed to clear records:', err);
      }
    });
  }

  // Manual Refresh Button
  elements.refreshBtn.addEventListener('click', () => {
    fetchLatestMetrics();
    fetchHistory();
    fetchFlags();
  });

  // Clear / Refresh Flags Button
  elements.clearFlagsBtn.addEventListener('click', fetchFlags);

  // Device select change
  elements.deviceSelect.addEventListener('change', (e) => {
    state.deviceId = e.target.value;
    state.lastRecordSeq = null;
    elements.chartLoader.style.display = 'flex';
    fetchLatestMetrics();
    fetchHistory();
    fetchFlags();
  });

  // Global Keyboard Shortcuts
  window.addEventListener('keydown', (e) => {
    if (e.key === 'r' && !['INPUT', 'SELECT', 'TEXTAREA'].includes(document.activeElement.tagName)) {
      e.preventDefault();
      fetchLatestMetrics();
      fetchHistory();
      fetchFlags();
    }
  });
}

/**
 * Real-Time Lead-II ECG Waveform Oscilloscope Engine
 * Synthesizes physiological P-Q-R-S-T potentials with sweep beam and medical grid.
 */
class ECGWaveformEngine {
  constructor(canvasId) {
    this.canvas = document.getElementById(canvasId);
    if (!this.canvas) return;
    this.ctx = this.canvas.getContext('2d');
    this.width = 0;
    this.height = 0;
    this.dpr = window.devicePixelRatio || 1;

    // Physiological & telemetry state
    this.isReceivingTelemetry = false;
    this.lastPacketTimeMs = 0;
    this.targetBpm = 72.0;
    this.currentBpm = 72.0;
    this.targetRrDuration = 0.833;
    this.signalQuality = 'valid';
    this.cyclePhaseSec = 0;
    this.sweepX = 0;
    this.sweepSpeedPxSec = 140; // 25 mm/s equivalent at 96 DPI
    this.lastTimeMs = performance.now();
    this.rPeakFiredThisBeat = false;

    // Real-time raw hardware ADC sample buffer queue
    this.rawSampleQueue = [];
    this.hasHardwareSamples = false;

    // Buffer for sweep trace: stores y position at each x coordinate
    this.points = [];

    this.resize();
    window.addEventListener('resize', () => this.resize());
    this.animate = this.animate.bind(this);
    requestAnimationFrame(this.animate);
  }

  resize() {
    if (!this.canvas) return;
    const rect = this.canvas.getBoundingClientRect();
    this.width = Math.floor(rect.width) || 800;
    this.height = Math.floor(rect.height) || 125;
    this.canvas.width = this.width * this.dpr;
    this.canvas.height = this.height * this.dpr;
    this.ctx.setTransform(1, 0, 0, 1, 0, 0); // reset transform
    this.ctx.scale(this.dpr, this.dpr);

    // Initialize points array: if offline, initialize to baseline isoelectric line
    this.points = new Array(this.width);
    const baselineY = this.height * 0.62;
    for (let x = 0; x < this.width; x++) {
      this.points[x] = baselineY;
    }
  }

  /**
   * Called dynamically when actual telemetry packets arrive from ESP32 / backend
   */
  updateMetrics(bpm, rrIntervalMs, quality, rawSamples) {
    this.lastPacketTimeMs = performance.now();
    this.isReceivingTelemetry = true;

    if (bpm && bpm >= 20 && bpm <= 260) {
      this.targetBpm = bpm;
    }
    if (rrIntervalMs && rrIntervalMs >= 100) {
      this.targetRrDuration = rrIntervalMs / 1000.0;
    } else if (bpm) {
      this.targetRrDuration = 60.0 / bpm;
    }
    if (quality) {
      this.signalQuality = quality;
    }

    // Ingest actual raw ADC / millivolt samples if the firmware sent them
    if (rawSamples && Array.isArray(rawSamples) && rawSamples.length > 0) {
      this.hasHardwareSamples = true;
      for (let i = 0; i < rawSamples.length; i++) {
        this.rawSampleQueue.push(rawSamples[i]);
      }
      // Cap buffer to avoid memory drift
      if (this.rawSampleQueue.length > 500) {
        this.rawSampleQueue = this.rawSampleQueue.slice(-250);
      }
    }
  }

  /**
   * P-Q-R-S-T Potential Generator [mV]
   * Computes cardiac voltage at phase t (seconds) in current RR cycle
   */
  getCardiacVoltage(t, rrDuration, allowStrobe = true) {
    if (this.signalQuality === 'invalid') {
      return (Math.random() - 0.5) * 0.05; // flatline / electrode lead-off
    }

    // Relative phase within cardiac cycle [0.0 .. 1.0]
    const p = (t % rrDuration) / rrDuration;

    let v = 0.0;

    // Baseline physiological tremor / respiratory drift
    const tremor = (this.signalQuality === 'degraded' ? 0.08 : 0.015) * (Math.random() - 0.5);
    v += tremor;

    // 1. P Wave (Atrial Depolarization) - smooth rounded dome
    const pCenter = 0.16;
    const pWidth = 0.04;
    v += 0.18 * Math.exp(-Math.pow((p - pCenter) / pWidth, 2));

    // 2. Q Wave (Septal Depolarization) - small negative dip
    const qCenter = 0.28;
    const qWidth = 0.015;
    v -= 0.16 * Math.exp(-Math.pow((p - qCenter) / qWidth, 2));

    // 3. R Peak (Ventricular Depolarization) - tall, sharp positive spike
    const rCenter = 0.32;
    const rWidth = 0.016;
    const rComponent = 1.35 * Math.exp(-Math.pow((p - rCenter) / rWidth, 2));
    v += rComponent;

    // Strobe R-peak when entering peak threshold
    if (allowStrobe && rComponent > 0.8 && !this.rPeakFiredThisBeat) {
      this.rPeakFiredThisBeat = true;
      triggerHeartPulse();
    } else if (p > 0.45) {
      this.rPeakFiredThisBeat = false;
    }

    // 4. S Wave (Late Ventricular Depol) - sharp negative undershoot
    const sCenter = 0.36;
    const sWidth = 0.016;
    v -= 0.42 * Math.exp(-Math.pow((p - sCenter) / sWidth, 2));

    // 5. T Wave (Ventricular Repolarization) - smooth rounded positive dome
    const tCenter = 0.55;
    const tWidth = 0.065;
    v += 0.35 * Math.exp(-Math.pow((p - tCenter) / tWidth, 2));

    return v;
  }

  animate(nowMs) {
    if (!this.canvas || !this.ctx) return;

    const dt = Math.min((nowMs - this.lastTimeMs) / 1000, 0.08); // cap at 80ms
    this.lastTimeMs = nowMs;

    // Detect if device has gone offline / no telemetry received for > 10 seconds
    if (nowMs - this.lastPacketTimeMs > 10000) {
      this.isReceivingTelemetry = false;
      this.rawSampleQueue = [];
      this.hasHardwareSamples = false;
    }

    const baselineY = this.height * 0.62;
    const mvToPx = 34; // 10mm / 1mV calibration
    const advancePx = Math.max(1, Math.round(this.sweepSpeedPxSec * dt));
    const startX = Math.floor(this.sweepX);

    if (!this.isReceivingTelemetry) {
      // Device is OFFLINE / Awaiting Acquisition:
      // Real ECG instruments flatline with slight analog idle thermal noise
      for (let i = 0; i < advancePx; i++) {
        const curX = (startX + i) % this.width;
        const noise = (Math.random() - 0.5) * 1.8;
        this.points[curX] = baselineY + noise;

        // Erase gap ahead of sweep beam
        const eraseAhead = 18;
        for (let e = 1; e <= eraseAhead; e++) {
          const eraseX = (curX + e) % this.width;
          this.points[eraseX] = null;
        }
      }
    } else if (this.rawSampleQueue.length > 0) {
      // Render ACTUAL raw hardware ADC samples received from ESP32 / simulator
      for (let i = 0; i < advancePx; i++) {
        const curX = (startX + i) % this.width;
        let v = 0.0;
        if (this.rawSampleQueue.length > 0) {
          v = this.rawSampleQueue.shift();
        }
        if (v > 0.8 && !this.rPeakFiredThisBeat) {
          this.rPeakFiredThisBeat = true;
          triggerHeartPulse();
        } else if (v < 0.2) {
          this.rPeakFiredThisBeat = false;
        }

        const y = baselineY - (v * mvToPx);
        this.points[curX] = y;

        const eraseAhead = 18;
        for (let e = 1; e <= eraseAhead; e++) {
          const eraseX = (curX + e) % this.width;
          this.points[eraseX] = null;
        }
      }
    } else {
      // Render real-time dynamically paced ECG waveform matching incoming RR intervals
      this.currentBpm += (this.targetBpm - this.currentBpm) * Math.min(dt * 4.0, 1.0);
      const rrDuration = this.targetRrDuration || (60.0 / Math.max(30, this.currentBpm));

      this.cyclePhaseSec = (this.cyclePhaseSec + dt) % rrDuration;

      for (let i = 0; i < advancePx; i++) {
        const curX = (startX + i) % this.width;
        const subDt = (i / advancePx) * dt;
        const phase = (this.cyclePhaseSec + subDt) % rrDuration;
        const v = this.getCardiacVoltage(phase, rrDuration);
        const y = baselineY - (v * mvToPx);
        this.points[curX] = y;

        const eraseAhead = 18;
        for (let e = 1; e <= eraseAhead; e++) {
          const eraseX = (curX + e) % this.width;
          this.points[eraseX] = null;
        }
      }
    }

    this.sweepX = (this.sweepX + advancePx) % this.width;

    // Render Canvas
    this.render();

    requestAnimationFrame(this.animate);
  }

  render() {
    const ctx = this.ctx;
    const w = this.width;
    const h = this.height;

    // Background
    ctx.fillStyle = '#04070a';
    ctx.fillRect(0, 0, w, h);

    // 1. Draw Precision ECG Medical Grid (1mm / 5mm standard grid)
    ctx.lineWidth = 1;
    
    // Minor grid (subtle 6px squares)
    ctx.strokeStyle = 'rgba(16, 185, 129, 0.04)';
    ctx.beginPath();
    for (let x = 0; x < w; x += 6) {
      ctx.moveTo(x, 0);
      ctx.lineTo(x, h);
    }
    for (let y = 0; y < h; y += 6) {
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
    }
    ctx.stroke();

    // Major grid (distinct 30px squares representing 0.2s / 0.5mV)
    ctx.strokeStyle = 'rgba(16, 185, 129, 0.12)';
    ctx.beginPath();
    for (let x = 0; x < w; x += 30) {
      ctx.moveTo(x, 0);
      ctx.lineTo(x, h);
    }
    for (let y = 0; y < h; y += 30) {
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
    }
    ctx.stroke();

    // Baseline indicator line
    const baselineY = Math.floor(h * 0.62);
    ctx.strokeStyle = 'rgba(56, 189, 248, 0.15)';
    ctx.beginPath();
    ctx.moveTo(0, baselineY);
    ctx.lineTo(w, baselineY);
    ctx.stroke();

    // 2. Draw Phosphor ECG Waveform Trace
    ctx.strokeStyle = this.isReceivingTelemetry ? '#00f090' : '#475569'; // Muted when offline, phosphor green when live
    ctx.lineWidth = 1.8;
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.shadowColor = this.isReceivingTelemetry ? 'rgba(0, 240, 144, 0.35)' : 'transparent';
    ctx.shadowBlur = this.isReceivingTelemetry ? 4 : 0;

    ctx.beginPath();
    let isDrawing = false;
    for (let x = 0; x < w; x++) {
      const y = this.points[x];
      if (y !== null && y !== undefined) {
        if (!isDrawing) {
          ctx.moveTo(x, y);
          isDrawing = true;
        } else {
          ctx.lineTo(x, y);
        }
      } else {
        isDrawing = false;
      }
    }
    ctx.stroke();

    // Reset shadow
    ctx.shadowBlur = 0;

    // 3. Draw Leading Sweep Head / Beam
    const headX = Math.floor(this.sweepX);
    const headY = this.points[headX] || baselineY;

    // Beam cursor glow
    ctx.fillStyle = this.isReceivingTelemetry ? '#ffffff' : '#94a3b8';
    ctx.beginPath();
    ctx.arc(headX, headY, 2.5, 0, Math.PI * 2);
    ctx.fill();

    // Vertical sweep shadow beam
    const gradient = ctx.createLinearGradient(headX - 16, 0, headX, 0);
    gradient.addColorStop(0, 'rgba(0, 240, 144, 0)');
    gradient.addColorStop(1, this.isReceivingTelemetry ? 'rgba(0, 240, 144, 0.12)' : 'rgba(100, 116, 139, 0.08)');
    ctx.fillStyle = gradient;
    ctx.fillRect(headX - 16, 0, 16, h);

    // 4. Draw Live Status HUD Badge
    ctx.font = '600 9px Inter, sans-serif';
    if (!this.isReceivingTelemetry) {
      ctx.fillStyle = '#64748b';
      ctx.fillText('STANDBY  •  AWAITING HARDWARE TELEMETRY', 12, 16);
    } else if (this.hasHardwareSamples) {
      ctx.fillStyle = '#00f090';
      ctx.fillText('● LIVE ACQUISITION  •  AD8232 HARDWARE SAMPLES', 12, 16);
    } else {
      ctx.fillStyle = '#00f090';
      ctx.fillText(`● LIVE ACQUISITION  •  RR ${Math.round(this.targetRrDuration * 1000)}ms  •  ${this.targetBpm.toFixed(1)} BPM`, 12, 16);
    }
  }
}

/**
 * Application Entry Point
 */
function init() {
  initChart();
  window.ecgEngine = new ECGWaveformEngine('ecgScopeCanvas');
  setupEventListeners();

  // Initial data ingestion
  fetchLatestMetrics();
  fetchHistory();
  fetchFlags();

  // Start polling every 5 seconds
  state.pollTimer = setInterval(() => {
    fetchLatestMetrics();
    fetchHistory();
  }, CONFIG.pollIntervalMs);
}

document.addEventListener('DOMContentLoaded', init);
