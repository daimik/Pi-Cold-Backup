"""Cold Backup API — Flask server called by ESP32 controller."""

from __future__ import annotations

import logging
import subprocess
import threading
import uuid
from datetime import datetime

from flask import Flask, jsonify

from .backup import BackupOrchestrator
from .config import Config, load_config
from .logger import setup_logging
from .models import BackupJob, BackupReport
from .notify import GotifyNotifier

log = logging.getLogger("cold-backup.api")

# Single backup job at a time
current_job: BackupJob = BackupJob()
job_lock = threading.Lock()
cfg: Config
notifier: GotifyNotifier


def create_app(config_path: str = "/etc/cold-backup/config.yaml") -> Flask:
    global cfg, notifier

    cfg = load_config(config_path)
    setup_logging(cfg.log_level, cfg.log_file)
    notifier = GotifyNotifier(cfg)

    app = Flask(__name__)

    @app.route("/api/health", methods=["GET"])
    def health():
        return jsonify({"status": "ready", "timestamp": datetime.now().isoformat()})

    @app.route("/api/backup/start", methods=["POST"])
    def backup_start():
        global current_job
        with job_lock:
            if current_job.state == "running":
                return jsonify({
                    "ok": False,
                    "error": "Backup already running",
                    "job_id": current_job.job_id,
                }), 409

            job_id = uuid.uuid4().hex[:8]
            current_job = BackupJob(
                job_id=job_id,
                state="running",
                started_at=datetime.now().isoformat(),
            )

        thread = threading.Thread(target=_run_backup, args=(job_id,), daemon=True)
        thread.start()
        return jsonify({"ok": True, "job_id": job_id}), 202

    @app.route("/api/backup/status", methods=["GET"])
    def backup_status():
        with job_lock:
            return jsonify(_job_to_dict(current_job))

    @app.route("/api/shutdown", methods=["POST"])
    def shutdown():
        """Graceful OS shutdown. ESP32 calls this before cutting relay power."""
        subprocess.Popen(["sudo", "shutdown", "-h", "+0", "Cold backup completed"])
        return jsonify({"ok": True, "message": "Shutting down in moments"})

    return app


def _run_backup(job_id: str) -> None:
    """Execute backup in background thread."""
    global current_job
    try:
        orchestrator = BackupOrchestrator(cfg)
        report = orchestrator.execute()

        notifier.send_report(report)

        with job_lock:
            if current_job.job_id == job_id:
                current_job.state = "completed" if report.status != "FAILED" else "failed"
                current_job.finished_at = datetime.now().isoformat()
                current_job.report = report

    except Exception as e:
        log.exception("Backup thread failed: %s", e)
        try:
            error_report = BackupReport(
                date=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                status="FAILED",
                errors=[f"Unhandled exception: {e}"],
            )
            notifier.send_report(error_report)
        except Exception:
            log.error("Failed to send crash notification")
        with job_lock:
            if current_job.job_id == job_id:
                current_job.state = "failed"
                current_job.finished_at = datetime.now().isoformat()
                current_job.error = str(e)


def _job_to_dict(job: BackupJob) -> dict:
    """Serialize BackupJob to JSON-safe dict."""
    result: dict = {
        "state": job.state,
        "job_id": job.job_id,
        "started_at": job.started_at,
        "finished_at": job.finished_at,
        "error": job.error,
        "report": None,
    }
    if job.report:
        r = job.report
        result["report"] = {
            "hostname": r.hostname,
            "date": r.date,
            "status": r.status,
            "duration": r.duration_human,
            "transferred_size": r.transferred_size,
            "total_backup_size": r.total_backup_size,
            "disk_free": r.disk_free,
            "disk_used_pct": r.disk_used_pct,
            "source": r.source,
            "destination": r.destination,
            "snapshots_kept": r.snapshots_kept,
            "snapshots_removed": r.snapshots_removed,
            "errors": r.errors,
        }
    return result
