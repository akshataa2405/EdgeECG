/**
 * EdgeECG Dashboard Application Logic
 * Integrates real-time telemetry polling, historical trend visualization with Chart.js,
 * connection state machine, and algorithmic deviation flag presentation.
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
  timeWindowMs: 3600000, // Default 1 hour
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
  timeWindowButtons: document.querySelectorAll('#time-window-group .btn-tab'),
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
          backgroundColor: 'rgba(56, 189, 248, 0.08)',
          borderWidth: 2,
          pointRadius: 2,
          pointHoverRadius: 5,
          tension: 0.25,
          fill: true,
          yAxisID: 'yHR',
        },
        {
          label: 'HRV RMSSD (ms)',
          data: [],
          borderColor: '#a78bfa',
          backgroundColor: 'transparent',
          borderWidth: 1.8,
          borderDash: [4, 4],
          pointRadius: 2,
          tension: 0.25,
          fill: false,
          yAxisID: 'yRMSSD',
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 300 },
      interaction: {
        mode: 'index',
        intersect: false,
      },
      plugins: {
        legend: {
          display: true,
          position: 'top',
          labels: {
            color: '#94a3b8',
            font: { family: "'Inter', sans-serif", size: 12 },
            usePointStyle: true,
          }
        },
        tooltip: {
          backgroundColor: '#0d121d',
          titleColor: '#f8fafc',
          bodyColor: '#94a3b8',
          borderColor: 'rgba(255, 255, 255, 0.1)',
          borderWidth: 1,
          padding: 10,
          titleFont: { family: "'JetBrains Mono', monospace" },
          bodyFont: { family: "'Inter', sans-serif" },
        }
      },
      scales: {
        x: {
          type: 'time',
          time: {
            displayFormats: {
              minute: 'HH:mm',
              hour: 'HH:mm',
              day: 'MMM d',
            }
          },
          grid: { color: 'rgba(255, 255, 255, 0.04)' },
          ticks: { color: '#64748b', font: { family: "'JetBrains Mono', monospace", size: 11 } }
        },
        yHR: {
          type: 'linear',
          position: 'left',
          title: { display: true, text: 'Heart Rate (BPM)', color: '#38bdf8', font: { size: 11 } },
          min: 30,
          max: 180,
          grid: { color: 'rgba(255, 255, 255, 0.04)' },
          ticks: { color: '#64748b', font: { family: "'JetBrains Mono', monospace", size: 11 } }
        },
        yRMSSD: {
          type: 'linear',
          position: 'right',
          title: { display: true, text: 'RMSSD (ms)', color: '#a78bfa', font: { size: 11 } },
          min: 0,
          max: 150,
          grid: { drawOnChartArea: false }, // avoid grid overlap
          ticks: { color: '#64748b', font: { family: "'JetBrains Mono', monospace", size: 11 } }
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
    elements.connectionBadge.className = 'badge badge-offline';
    elements.connectionStatusText.textContent = 'NO TELEMETRY';
    elements.deviceSyncState.textContent = 'Unpaired';
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
    elements.connectionBadge.className = 'badge badge-connected';
    elements.connectionStatusText.textContent = 'CONNECTED';
    elements.deviceSyncState.textContent = 'Streaming';
  } else if (ageMs <= CONFIG.offlineThresholdMs) {
    elements.connectionBadge.className = 'badge badge-stale';
    elements.connectionStatusText.textContent = 'STALE';
    elements.deviceSyncState.textContent = 'Delayed';
  } else {
    elements.connectionBadge.className = 'badge badge-offline';
    elements.connectionStatusText.textContent = 'OFFLINE';
    elements.deviceSyncState.textContent = 'Buffering Offline';
  }

  // Update Signal Quality Badge
  if (signalQuality === 'valid') {
    elements.qualityBadge.className = 'badge badge-connected';
    elements.qualityStatusText.textContent = 'SIGNAL OPTIMAL';
  } else if (signalQuality === 'degraded') {
    elements.qualityBadge.className = 'badge badge-stale';
    elements.qualityStatusText.textContent = 'SIGNAL NOISY';
  } else if (signalQuality === 'invalid') {
    elements.qualityBadge.className = 'badge badge-offline';
    elements.qualityStatusText.textContent = 'LEAD OFF / SATURATED';
  } else {
    elements.qualityBadge.className = 'badge badge-neutral';
    elements.qualityStatusText.textContent = 'NO SIGNAL';
  }
}

/**
 * Trigger Visual Pulse Animation
 */
function triggerHeartPulse() {
  elements.hrPulse.classList.add('pulse-active');
  setTimeout(() => {
    elements.hrPulse.classList.remove('pulse-active');
  }, 180);
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
      // Pulse animation if new sequence received
      if (state.lastRecordSeq !== rec.sequence) {
        triggerHeartPulse();
        state.lastRecordSeq = rec.sequence;
      }

      // Heart Rate
      if (rec.heart_rate_bpm !== null) {
        elements.hrValue.textContent = rec.heart_rate_bpm.toFixed(1);
        if (rec.heart_rate_bpm < 50) {
          elements.hrStatusHint.textContent = 'Bradycardia Range';
          elements.hrStatusHint.style.color = 'var(--accent-cyan)';
        } else if (rec.heart_rate_bpm > 120) {
          elements.hrStatusHint.textContent = 'Tachycardia Range';
          elements.hrStatusHint.style.color = 'var(--status-danger)';
        } else {
          elements.hrStatusHint.textContent = 'Resting Range';
          elements.hrStatusHint.style.color = 'var(--status-success)';
        }
      } else {
        elements.hrValue.textContent = '--';
        elements.hrStatusHint.textContent = 'Signal invalid';
      }

      // RMSSD
      if (rec.rmssd_ms !== null) {
        elements.rmssdValue.textContent = rec.rmssd_ms.toFixed(1);
        elements.rmssdStatusHint.textContent = 'Adequate beat density';
      } else {
        elements.rmssdValue.textContent = '--';
        elements.rmssdStatusHint.textContent = 'Needs ≥ 10 valid intervals';
      }

      // RR Interval
      if (rec.rr_interval_ms !== null) {
        elements.rrValue.textContent = Math.round(rec.rr_interval_ms);
      } else {
        elements.rrValue.textContent = '--';
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
    const sinceMs = Date.now() - state.timeWindowMs;
    const url = `${CONFIG.apiBaseUrl}/api/v1/devices/${state.deviceId}/records?since_ms=${sinceMs}&limit=500`;

    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);

    const data = await res.json();
    const records = data.records || [];

    if (records.length === 0) {
      elements.chartEmptyState.hidden = false;
      state.chart.data.datasets[0].data = [];
      state.chart.data.datasets[1].data = [];
      state.chart.update();
      return;
    }

    elements.chartEmptyState.hidden = true;

    const hrPoints = [];
    const rmssdPoints = [];

    records.forEach(r => {
      const t = new Date(r.timestamp_ms);
      if (r.heart_rate_bpm !== null) {
        hrPoints.push({ x: t, y: r.heart_rate_bpm });
      }
      if (r.rmssd_ms !== null) {
        rmssdPoints.push({ x: t, y: r.rmssd_ms });
      }
    });

    state.chart.data.datasets[0].data = hrPoints;
    state.chart.data.datasets[1].data = rmssdPoints;
    state.chart.update();
  } catch (err) {
    console.error('Error fetching history:', err);
  } finally {
    state.isFetchingHistory = false;
    elements.chartLoader.hidden = true;
  }
}

/**
 * Fetch Deviation Flags
 */
async function fetchFlags() {
  try {
    const url = `${CONFIG.apiBaseUrl}/api/v1/devices/${state.deviceId}/flags?limit=50`;
    const res = await fetch(url);
    if (!res.ok) return;

    const data = await res.json();
    const flags = data.flags || [];

    if (flags.length === 0) {
      elements.flagsTbody.innerHTML = `
        <tr>
          <td colspan="6" class="text-center text-muted" style="padding: 1.5rem;">No interval deviations or physiological flags registered.</td>
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
          <td style="font-family: var(--font-mono);">${timeStr}</td>
          <td style="font-family: var(--font-mono);">#${f.sequence}</td>
          <td style="font-family: var(--font-mono);">${hrStr}</td>
          <td style="font-family: var(--font-mono);">${rrStr}</td>
          <td><span class="flag-pill ${f.flag_type}">${f.flag_type}</span></td>
          <td>${f.description}</td>
        </tr>
      `;
    }).join('');
  } catch (err) {
    console.error('Error fetching flags:', err);
  }
}

/**
 * Setup Event Listeners
 */
function setupEventListeners() {
  // Time Window Tabs
  elements.timeWindowButtons.forEach(btn => {
    btn.addEventListener('click', () => {
      elements.timeWindowButtons.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      state.timeWindowMs = parseInt(btn.dataset.window, 10);
      elements.chartLoader.hidden = false;
      fetchHistory();
    });
  });

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
    elements.chartLoader.hidden = false;
    fetchLatestMetrics();
    fetchHistory();
    fetchFlags();
  });
}

/**
 * Application Entry Point
 */
function init() {
  initChart();
  setupEventListeners();

  // Initial fetches
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
