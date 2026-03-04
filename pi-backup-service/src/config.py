"""Configuration loader — YAML file with optional env var overrides for secrets."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

import yaml


@dataclass(frozen=True)
class Config:
    """Immutable configuration for the backup service."""

    # API server
    api_host: str = "0.0.0.0"
    api_port: int = 5000

    # SMB source
    smb_ip: str = ""
    smb_share: str = ""
    smb_user: str = ""
    smb_pass: str = ""
    smb_mount_point: str = "/mnt/smb-backup"
    smb_mount_options: str = "vers=3.0,seal,uid=0,gid=0,file_mode=0600,dir_mode=0700,iocharset=utf8"

    # Backup
    backup_sources: list[str] = field(default_factory=list)
    backup_dest: str = "/backup/cold-storage"
    backup_excludes: list[str] = field(default_factory=list)
    rsync_options: str = "-avh --stats --delete --partial --timeout=600"

    # Retention
    retention_keep: int = 6
    retention_dir: str = "/backup/snapshots"

    # Gotify
    gotify_url: str = ""
    gotify_token: str = ""

    # Post-backup
    post_action: str = "shutdown"

    # Logging
    log_level: str = "INFO"
    log_file: str = "/var/log/cold-backup/backup.log"


def load_config(config_path: str = "/etc/cold-backup/config.yaml") -> Config:
    """Load from YAML file, allow env var overrides for secrets."""
    data: dict = {}
    path = Path(config_path)
    if path.exists():
        with open(path) as f:
            data = yaml.safe_load(f) or {}

    smb = data.get("smb", {})
    backup = data.get("backup", {})
    retention = data.get("retention", {})
    gotify = data.get("gotify", {})
    api = data.get("api", {})
    logging_cfg = data.get("logging", {})

    return Config(
        api_host=api.get("host", "0.0.0.0"),
        api_port=int(api.get("port", 5000)),
        smb_ip=os.environ.get("SMB_SOURCE_IP", smb.get("ip", "")),
        smb_share=os.environ.get("SMB_SHARE", smb.get("share", "")),
        smb_user=os.environ.get("SMB_USER", smb.get("user", "")),
        smb_pass=os.environ.get("SMB_PASS", smb.get("pass", "")),
        smb_mount_point=smb.get("mount_point", "/mnt/smb-backup"),
        smb_mount_options=smb.get("mount_options",
                                   "vers=3.0,seal,uid=0,gid=0,file_mode=0600,dir_mode=0700,iocharset=utf8"),
        backup_sources=backup.get("sources", ["Documents"]),
        backup_dest=backup.get("dest", "/backup/cold-storage"),
        backup_excludes=backup.get("excludes", []),
        rsync_options=backup.get("rsync_options", "-avh --stats --delete --partial --timeout=600"),
        retention_keep=int(retention.get("keep", 6)),
        retention_dir=retention.get("dir", "/backup/snapshots"),
        gotify_url=os.environ.get("GOTIFY_URL", gotify.get("url", "")),
        gotify_token=os.environ.get("GOTIFY_API", gotify.get("token", "")),
        post_action=data.get("post_action", "shutdown"),
        log_level=logging_cfg.get("level", "INFO"),
        log_file=logging_cfg.get("file", "/var/log/cold-backup/backup.log"),
    )
