// Cold Backup ESP32 Dashboard

function $(id) { return document.getElementById(id); }

function fmt(ms) {
    if (!ms || ms <= 0) return '--';
    var s = Math.floor(ms / 1000);
    var h = Math.floor(s / 3600);
    var m = Math.floor((s % 3600) / 60);
    if (h > 0) return h + 'h ' + m + 'm';
    if (m > 0) return m + 'm ' + (s % 60) + 's';
    return s + 's';
}

function setConnected(ok) {
    var el = $('conn-status');
    if (ok) {
        el.textContent = 'Connected';
        el.className = 'badge on';
    } else {
        el.textContent = 'Offline';
        el.className = 'badge off';
    }
}

function fetchStatus() {
    fetch('/api/status').then(function(r) {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.json();
    }).then(function(d) {
        setConnected(true);

        // Ethernet
        var eth = $('eth-status');
        if (d.eth_connected) {
            eth.textContent = d.eth_ip;
            eth.className = 'badge on';
        } else {
            eth.textContent = 'No Link';
            eth.className = 'badge off';
        }

        // Pi power
        var pw = $('pi-power');
        pw.className = d.pi_power ? 'dot on' : 'dot off';

        // State
        $('state').textContent = d.state;

        // Last backup
        $('last-backup').textContent = d.last_backup_ago_ms ? fmt(d.last_backup_ago_ms) + ' ago' : '--';
        $('last-result').textContent = d.last_result || '--';
        $('last-duration').textContent = d.last_duration_ms ? fmt(d.last_duration_ms) : '--';
        $('last-error').textContent = d.last_error || '--';

        // Next run
        $('next-run').textContent = fmt(d.next_run_ms);

        // Uptime
        $('uptime').textContent = fmt(d.uptime_ms);

        // Heap
        $('heap').textContent = d.free_heap ? Math.round(d.free_heap / 1024) + ' KB' : '--';
    }).catch(function() {
        setConnected(false);
    });
}

function loadConfig() {
    fetch('/api/config').then(function(r) {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.json();
    }).then(function(d) {
        $('relay_pin').value = d.relay_pin;
        $('pi_ip').value = d.pi_ip;
        $('pi_port').value = d.pi_port;
        $('schedule_interval_min').value = d.schedule_interval_min;
    }).catch(function() {});
}

function saveConfig(e) {
    e.preventDefault();
    var body = {
        relay_pin: parseInt($('relay_pin').value),
        pi_ip: $('pi_ip').value,
        pi_port: parseInt($('pi_port').value),
        schedule_interval_min: parseInt($('schedule_interval_min').value)
    };
    fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
    }).then(function(r) {
        if (!r.ok) return r.json().then(function(d) { throw new Error(d.error || 'Save failed'); });
        return r.json();
    }).then(function(d) {
        if (d.ok) alert('Config saved');
        else alert(d.error || 'Save failed');
    }).catch(function(e) { alert(e.message || 'Save failed'); });
}

function powerOn() {
    fetch('/api/power/on', { method: 'POST' }).then(function() { fetchStatus(); });
}

function powerOff() {
    if (!confirm('Cut power to Pi?')) return;
    fetch('/api/power/off', { method: 'POST' }).then(function() { fetchStatus(); });
}

function rebootEsp() {
    if (!confirm('Reboot ESP32 controller?')) return;
    fetch('/api/reboot', { method: 'POST' }).then(function() {
        setConnected(false);
    });
}

function triggerBackup() {
    fetch('/api/backup/trigger', { method: 'POST' }).then(function(r) {
        if (!r.ok) return r.json().then(function(d) { alert(d.error || 'Failed'); });
        fetchStatus();
    }).catch(function() { alert('Failed to reach ESP32'); });
}

// Init
fetchStatus();
loadConfig();
setInterval(fetchStatus, 5000);
