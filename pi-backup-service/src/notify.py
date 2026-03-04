"""Gotify push notification client."""

from __future__ import annotations

import logging

import requests

from .config import Config
from .models import BackupReport

log = logging.getLogger("cold-backup.notify")


class GotifyNotifier:
    """Send notifications to Gotify server."""

    def __init__(self, cfg: Config) -> None:
        self.url = cfg.gotify_url
        self.token = cfg.gotify_token
        self.enabled = bool(self.url and self.token)

    def send(self, title: str, message: str, priority: int = 5) -> bool:
        """Send a message to Gotify. Returns True on success."""
        if not self.enabled:
            log.warning("Gotify not configured — skipping notification")
            return False

        try:
            resp = requests.post(
                f"{self.url}/message",
                json={
                    "title": title,
                    "message": message,
                    "priority": priority,
                    "extras": {"client::display": {"contentType": "text/markdown"}},
                },
                headers={"X-Gotify-Key": self.token},
                timeout=30,
            )
            if resp.status_code == 200:
                log.info("Gotify notification sent: %s", title)
                return True
            else:
                log.warning("Gotify HTTP %d: %s", resp.status_code, resp.text[:200])
                return False
        except requests.RequestException as e:
            log.error("Gotify request failed: %s", e)
            return False

    def send_report(self, report: BackupReport) -> bool:
        """Send a formatted backup report."""
        is_ok = report.status == "SUCCESS"
        icon = "✅" if is_ok else "❌"
        priority = 5 if is_ok else 8

        lines = [
            f"### {icon} {report.status} · `{report.hostname}`",
            f"📅 Started: {report.date}",
            f"⏱️ Duration: {report.duration_human}",
            f"📤 Transferred: {report.transferred_size}",
            f"💾 Backup size: {report.total_backup_size}",
            f"💿 Free: {report.disk_free} ({report.disk_used_pct} used)",
            f"📥 Source: `{report.source}`",
            f"📂 Dest: `{report.destination}`",
        ]

        snap_parts = []
        if report.snapshots_kept > 0:
            snap_parts.append(f"📌 Kept: {report.snapshots_kept}")
        if report.snapshots_removed > 0:
            snap_parts.append(f"🗑️ Removed: {report.snapshots_removed}")
        if snap_parts:
            lines.append(" · ".join(snap_parts))

        for err in report.errors:
            lines.append(f"⚠️ {err}")

        return self.send(f"🍓 Pi Backup {report.status}",
                         "  \n".join(lines), priority)
