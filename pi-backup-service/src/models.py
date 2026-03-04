"""Shared data models for the cold backup service."""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class CommandResult:
    """Result of a local command execution."""

    command: str = ""
    stdout: str = ""
    stderr: str = ""
    exit_code: int = -1
    duration_seconds: float = 0.0

    @property
    def ok(self) -> bool:
        return self.exit_code == 0


@dataclass
class BackupContext:
    """Tracks state across the backup pipeline."""

    start_time: float = 0.0
    errors: list[str] = field(default_factory=list)
    smb_mounted: bool = False
    rsync_stats: dict[str, str] = field(default_factory=dict)
    sources_synced: int = 0
    sources_failed: int = 0
    snapshots_kept: int = 0
    snapshots_removed: int = 0

    def add_error(self, msg: str) -> None:
        self.errors.append(msg)

    @property
    def status(self) -> str:
        if self.sources_synced == 0 and self.sources_failed > 0:
            return "FAILED"
        if self.errors:
            return "PARTIAL"
        return "SUCCESS"


@dataclass
class BackupReport:
    """Structured backup result for notification and API response."""

    hostname: str = ""
    date: str = ""
    status: str = "UNKNOWN"
    duration_human: str = ""
    transferred_size: str = ""
    total_backup_size: str = ""
    disk_free: str = ""
    disk_used_pct: str = ""
    source: str = ""
    destination: str = ""
    snapshots_kept: int = 0
    snapshots_removed: int = 0
    errors: list[str] = field(default_factory=list)


@dataclass
class BackupJob:
    """Tracks the current backup job for the API."""

    job_id: str = ""
    state: str = "idle"  # idle | running | completed | failed
    started_at: str = ""
    finished_at: str = ""
    report: BackupReport | None = None
    error: str = ""
