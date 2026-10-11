(() => {
  const dialog = document.getElementById('healthChartDialog');
  const closeButton = document.getElementById('healthDialogClose');
  const title = document.getElementById('healthDialogTitle');
  const noData = document.getElementById('healthNoData');
  const chartArea = document.querySelector('.health-chart-card');

  const liveStatus = document.getElementById('healthLiveStatus');
  const gnssDiagnostic = document.getElementById('healthGnssDiagnostic');
  let chart = null;
  let liveTimer = null;
  let activeBoxId = null;
  const LIVE_REFRESH_MS = 5000;

  function stopLive() {
    if (liveTimer !== null) {
      window.clearInterval(liveTimer);
      liveTimer = null;
    }
    activeBoxId = null;
  }

  function setLiveStatus(message, stale = false) {
    liveStatus.classList.toggle('is-stale', stale);
    liveStatus.innerHTML = '<span class="health-live-dot"></span>' + message;
  }

  function destroyChart() {
    if (chart) {
      chart.destroy();
      chart = null;
    }
  }

  function renderChart(series) {
    destroyChart();
    const canvas = document.getElementById('healthCombinedChart');

    // Axes adapt to the actual measurements, with a little breathing room.
    // Voltage is kept in a narrow, readable range; current and power start at zero
    // only when that does not flatten the useful variations.
    function paddedRange(values, minimumSpan, paddingRatio = 0.18, floor = null) {
      const valid = values.filter(Number.isFinite);
      if (!valid.length) return { min: 0, max: minimumSpan };
      let low = Math.min(...valid);
      let high = Math.max(...valid);
      if (high - low < minimumSpan) {
        const middle = (high + low) / 2;
        low = middle - minimumSpan / 2;
        high = middle + minimumSpan / 2;
      }
      const pad = (high - low) * paddingRatio;
      low -= pad;
      high += pad;
      if (floor !== null) low = Math.max(floor, low);
      return { min: low, max: high };
    }

    const voltageRange = paddedRange(series.map(p => p.volt), 0.20, 0.10);
    const currentRange = paddedRange(series.map(p => p.amp), 0.10, 0.10, 0);
    const powerRange = paddedRange(series.map(p => p.power), 0.50, 0.10, 0);
    chart = new Chart(canvas, {
      type: 'line',
      data: {
        labels: series.map(point => point.time),
        datasets: [
          {
            label: 'Tension U (V)',
            data: series.map(point => point.volt),
            borderColor: '#6d35ff',
            backgroundColor: 'rgba(109,53,255,.08)',
            yAxisID: 'yVoltage',

            pointRadius: 0,
            pointHoverRadius: 0,
            pointHitRadius: 8,
            borderWidth: 2,
            tension: .38,
            spanGaps: false
          },
          {
            label: 'Courant I (A)',
            data: series.map(point => point.amp),
            borderColor: '#0891b2',
            backgroundColor: 'rgba(8,145,178,.08)',
            yAxisID: 'yCurrent',

            pointRadius: 0,
            pointHoverRadius: 0,
            pointHitRadius: 8,
            borderWidth: 2,
            tension: .32,
            spanGaps: false
          },
          {
            label: 'Puissance P (W)',
            data: series.map(point => point.power),
            borderColor: '#e58b19',
            backgroundColor: 'rgba(229,139,25,.08)',
            yAxisID: 'yPower',

            pointRadius: 0,
            pointHoverRadius: 0,
            pointHitRadius: 8,
            borderWidth: 2,
            tension: .18,
            spanGaps: false
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        interaction: { mode: 'index', intersect: false },
        plugins: {
          legend: { display: true, position: 'top' },
          tooltip: { mode: 'index', intersect: false }
        },
        scales: {
          x: { title: { display: true, text: 'Heure locale' }, ticks: { maxTicksLimit: 10, maxRotation: 0 } },
          yVoltage: {
            type: 'linear', position: 'left',
            min: voltageRange.min, max: voltageRange.max,
            title: { display: true, text: 'U (V)', color:'#6d35ff' },
            ticks: { maxTicksLimit: 7, precision: 2, color:'#6d35ff' },
            grid: { drawOnChartArea: true }
          },
          yCurrent: {
            type: 'linear', position: 'right',
            min: currentRange.min, max: currentRange.max,
            title: { display: true, text: 'I (A)', color:'#0891b2' },
            ticks: { maxTicksLimit: 7, precision: 2, color: '#0891b2' },
            grid: { drawOnChartArea: false }
          },
          yPower: {
            type: 'linear', position: 'right',
            min: powerRange.min, max: powerRange.max,
            title: { display: true, text: 'P (W)', color:'#e58b19' },
            ticks: { maxTicksLimit: 7, precision: 2, color:'#e58b19' },
            grid: { drawOnChartArea: false },
            offset: true,
            display: true
          }
        }
      }
    });
    syncAxisVisibility();
  }

  function syncAxisVisibility() {
    if (!chart) return;
    const visibleByAxis = { yVoltage: false, yCurrent: false, yPower: false };
    chart.data.datasets.forEach((dataset, index) => {
      if (dataset.yAxisID && Object.prototype.hasOwnProperty.call(visibleByAxis, dataset.yAxisID)) {
        visibleByAxis[dataset.yAxisID] = chart.isDatasetVisible(index);
      }
    });
    Object.entries(visibleByAxis).forEach(([axisId, visible]) => {
      chart.options.scales[axisId].display = visible;
    });
    chart.update('none');
  }



  function paddedRangeForLive(values, minimumSpan, paddingRatio = 0.10, floor = null) {
    const valid = values.filter(Number.isFinite);
    if (!valid.length) return { min: 0, max: minimumSpan };
    let low = Math.min(...valid);
    let high = Math.max(...valid);
    if (high - low < minimumSpan) {
      const middle = (high + low) / 2;
      low = middle - minimumSpan / 2;
      high = middle + minimumSpan / 2;
    }
    const pad = (high - low) * paddingRatio;
    low -= pad;
    high += pad;
    if (floor !== null) low = Math.max(floor, low);
    return { min: low, max: high };
  }

  async function refreshLive() {
    if (!activeBoxId) return;
    try {
      const response = await fetch('health.php?live=1&trak_box_id=' + encodeURIComponent(activeBoxId), {
        cache: 'no-store',
        headers: { 'Accept': 'application/json' }
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const payload = await response.json();
      if (!payload.ok) throw new Error(payload.error || 'Réponse invalide');
      const diagnostic = payload.diagnostic;
      const isOffline = payload.online === false;
      if (gnssDiagnostic) {
        const n = value => value === null || value === undefined || value === '' ? '—' : value;
        const setValue = (id, value) => { const el = document.getElementById(id); if (el) el.textContent = n(value); };
        const fixBadge = document.getElementById('healthGnssFix');
        if (isOffline) {
          setValue('healthGnssReceived', payload.last_position_received_at ? 'Dernier contact : ' + new Date(payload.last_position_received_at.replace(' ', 'T') + 'Z').toLocaleString('fr-FR') : 'Aucun contact reçu');
          ['healthGnssVisible','healthGnssUsed','healthGnssGps','healthGnssGalileo','healthGnssGlonass','healthGnssBeidou','healthGnssHdop','healthGnssPdop','healthGnssVdop'].forEach(id => setValue(id, null));
          if (fixBadge) { fixBadge.textContent = 'Hors ligne'; fixBadge.className = 'health-gnss-status is-no-fix'; }
        } else if (!diagnostic) {
          setValue('healthGnssReceived', 'Aucun diagnostic reçu');
          ['healthGnssVisible','healthGnssUsed','healthGnssGps','healthGnssGalileo','healthGnssGlonass','healthGnssBeidou','healthGnssHdop','healthGnssPdop','healthGnssVdop'].forEach(id => setValue(id, null));
          if (fixBadge) { fixBadge.textContent = 'Aucune donnée'; fixBadge.className = 'health-gnss-status'; }
        } else {
          const fixed = diagnostic.gnss_fix !== null && diagnostic.gnss_fix !== undefined
            ? diagnostic.gnss_fix === true
            : Number(diagnostic.fix_mode) >= 2;
          const fix = fixed ? (Number(diagnostic.fix_mode) === 3 ? 'FIX 3D' : 'FIX') : 'Sans fix';
          if (fixBadge) { fixBadge.textContent = fix; fixBadge.className = 'health-gnss-status ' + (fixed ? 'is-fixed' : 'is-no-fix'); }
          setValue('healthGnssReceived', diagnostic.received_at ? 'Reçu le ' + new Date(diagnostic.received_at.replace(' ', 'T') + 'Z').toLocaleString('fr-FR') : 'Dernier diagnostic');
          setValue('healthGnssVisible', diagnostic.satellites_total);
          setValue('healthGnssUsed', diagnostic.satellites_used);
          setValue('healthGnssGps', diagnostic.gps_satellites);
          setValue('healthGnssGalileo', diagnostic.galileo_satellites);
          setValue('healthGnssGlonass', diagnostic.glonass_satellites);
          setValue('healthGnssBeidou', diagnostic.beidou_satellites);
          setValue('healthGnssHdop', diagnostic.hdop);
          setValue('healthGnssPdop', diagnostic.pdop);
          setValue('healthGnssVdop', diagnostic.vdop);
        }
      }
      const series = Array.isArray(payload.series) ? payload.series : [];
      if (series.length) {
        if (!chart && typeof Chart !== 'undefined') renderChart(series);
        if (chart) {
          chart.data.labels = series.map(point => point.time);
          chart.data.datasets[0].data = series.map(point => point.volt);
          chart.data.datasets[1].data = series.map(point => point.amp);
          chart.data.datasets[2].data = series.map(point => point.power);
          const ranges = [
            paddedRangeForLive(series.map(point => point.volt), 0.20, 0.10),
            paddedRangeForLive(series.map(point => point.amp), 0.10, 0.10, 0),
            paddedRangeForLive(series.map(point => point.power), 0.50, 0.10, 0)
          ];
          chart.options.scales.yVoltage.min = ranges[0].min;
          chart.options.scales.yVoltage.max = ranges[0].max;
          chart.options.scales.yCurrent.min = ranges[1].min;
          chart.options.scales.yCurrent.max = ranges[1].max;
          chart.options.scales.yPower.min = ranges[2].min;
          chart.options.scales.yPower.max = ranges[2].max;
          syncAxisVisibility();
          noData.hidden = true;
          chartArea.hidden = false;
          
        }
      } else if (!chart) {
        noData.hidden = false;
        chartArea.hidden = true;

      }
      setLiveStatus('LIVE — actualisé à ' + new Date().toLocaleTimeString('fr-FR'));
    } catch (error) {
      setLiveStatus('LIVE — connexion en attente, nouvelle tentative…', true);
    }
  }

  document.querySelectorAll('.health-view-btn').forEach(button => {
    button.addEventListener('click', () => {
      stopLive();
      activeBoxId = button.dataset.trakBoxId;
      let series = [];
      try { series = JSON.parse(button.dataset.series || '[]'); } catch (e) {}
      title.textContent = 'Santé — ' + button.dataset.trak;
      noData.hidden = series.length > 0;
      chartArea.hidden = series.length === 0;
      if (series.length > 0 && typeof Chart !== 'undefined') renderChart(series);
      else destroyChart();
      if (typeof dialog.showModal === 'function') dialog.showModal();
      setLiveStatus('LIVE — connexion en cours…');
      refreshLive();
      liveTimer = window.setInterval(refreshLive, LIVE_REFRESH_MS);
    });
  });

  closeButton.addEventListener('click', () => dialog.close());
  dialog.addEventListener('click', event => {
    if (event.target === dialog) dialog.close();
  });
  dialog.addEventListener('close', () => {
    stopLive();
    destroyChart();
  });
})();
