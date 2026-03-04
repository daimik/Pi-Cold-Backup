"""Backup orchestrator — SMB mount, rsync, retention via local subprocess."""

from __future__ import annotations

import logging
import os
import re
import shlex
import subprocess
import time
from datetime import datetime

from .config import Config
from .models import BackupContext, BackupReport, CommandResult

log = logging.getLogger("cold-backup.backup")


def format_duration(seconds: float) -> str:
    s = int(seconds)
    h, remainder = divmod(s, 3600)
    m, sec = divmod(remainder, 60)
    return f"{h}h {m}m {sec}s"


class BackupOrchestrator:
    """Execute the full backup workflow using local commands."""

    def __init__(self, cfg: Config) -> None:
        self.cfg = cfg
        self.ctx = BackupContext(start_time=time.time())

    def _run(self, command: str, timeout: int = 3600) -> CommandResult:
        """Execute a local shell command."""
        log.debug("Executing: %s", command)
        start = time.time()
        try:
            proc = subprocess.run(
                command, shell=True,
                capture_output=True, text=True, timeout=timeout,
            )
            result = CommandResult(
                command=command,
                stdout=proc.stdout.strip(),
                stderr=proc.stderr.strip(),
                exit_code=proc.returncode,
                duration_seconds=time.time() - start,
            )
            if result.ok:
                log.debug("Command OK (%.1fs)", result.duration_seconds)
            else:
                log.warning("Command FAIL (exit %d, %.1fs): %s",
                            result.exit_code, result.duration_seconds,
                            result.stderr[:200] or "(no stderr)")
            return result
        except subprocess.TimeoutExpired:
            return CommandResult(command=command, stderr="Command timed out",
                                exit_code=-1, duration_seconds=time.time() - start)
        except Exception as e:
            return CommandResult(command=command, stderr=str(e),
                                exit_code=-1, duration_seconds=time.time() - start)

    # ── SMB Mount ────────────────────────────────────────────────────────

    def mount_smb(self) -> bool:
        smb_path = f"//{self.cfg.smb_ip}/{self.cfg.smb_share}"
        mp = self.cfg.smb_mount_point
        log.info("Mounting SMB: %s → %s", smb_path, mp)

        r = self._run(f"sudo mkdir -p {mp}")
        if not r.ok:
            self.ctx.add_error(f"Failed to create mount point: {r.stderr}")
            log.error(self.ctx.errors[-1])
            return False

        # Unmount if already mounted
        r = self._run(f"sudo mountpoint -q {mp}")
        if r.ok:
            log.info("Already mounted — unmounting first")
            self._run(f"sudo umount {mp}")
            time.sleep(2)

        # Write temp credentials (direct file I/O — no shell escaping needed)
        creds = "/tmp/.smb_backup_creds"
        with open(creds, "w") as f:
            f.write(f"username={self.cfg.smb_user}\n")
            f.write(f"password={self.cfg.smb_pass}\n")
            f.write("domain=WORKGROUP\n")
        os.chmod(creds, 0o600)

        # Mount
        mount_cmd = (
            f"sudo mount -t cifs {smb_path} {mp} "
            f"-o credentials={creds},{self.cfg.smb_mount_options}"
        )
        r = self._run(mount_cmd)

        # Clean up credentials immediately
        try:
            os.remove(creds)
        except OSError:
            pass

        if not r.ok:
            self.ctx.add_error(f"SMB mount failed: {r.stderr}")
            log.error(self.ctx.errors[-1])
            return False

        self.ctx.smb_mounted = True
        log.info("SMB mounted successfully")

        r = self._run(f"sudo ls {shlex.quote(mp + '/')} | head -20")
        if r.ok:
            log.info("Mount contents: %s", r.stdout[:500])
        else:
            log.warning("Could not list mount contents")
        return True

    def unmount_smb(self) -> None:
        if not self.ctx.smb_mounted:
            return
        log.info("Unmounting SMB...")
        r = self._run(f"sudo umount {self.cfg.smb_mount_point}")
        if not r.ok:
            self._run(f"sudo umount -l {self.cfg.smb_mount_point}")
        self.ctx.smb_mounted = False
        log.info("SMB unmounted")

    # ── Rsync ────────────────────────────────────────────────────────────

    def run_rsync(self) -> bool:
        cfg = self.cfg
        use_snapshots = cfg.retention_keep > 0

        if use_snapshots:
            snapshot_name = datetime.now().strftime("%Y-%m-%d_%H%M%S")
            dest_base = f"{cfg.retention_dir}/{snapshot_name}"
            latest_link = f"{cfg.retention_dir}/latest"

            link_dest_opt = ""
            r = self._run(f"readlink -f {latest_link} 2>/dev/null || echo ''")
            prev = r.stdout.strip()
            if prev and prev != latest_link:
                link_dest_opt = f"--link-dest={prev}"
                log.info("Hardlink base: %s", prev)

            self._run(f"sudo mkdir -p {dest_base}")
        else:
            dest_base = cfg.backup_dest
            link_dest_opt = ""
            self._run(f"sudo mkdir -p {dest_base}")

        excludes = " ".join(f"--exclude='{e}'" for e in cfg.backup_excludes)

        all_ok = True
        for source in cfg.backup_sources:
            src_path = f"{cfg.smb_mount_point}/{source}"

            r = self._run(f"sudo test -e {shlex.quote(src_path)}")
            if not r.ok:
                r_ls = self._run(f"sudo ls -1 {shlex.quote(cfg.smb_mount_point + '/')}")
                available = r_ls.stdout.strip() if r_ls.ok else "(could not list)"
                self.ctx.add_error(
                    f"Source not found: {source} — available on share: {available}"
                )
                log.error(self.ctx.errors[-1])
                self.ctx.sources_failed += 1
                continue

            dest_path = f"{dest_base}/{source}"
            self._run(f"sudo mkdir -p {shlex.quote(dest_path)}")

            rsync_cmd = re.sub(
                r"\s+", " ",
                f"sudo rsync {cfg.rsync_options} {excludes} {link_dest_opt} "
                f"{shlex.quote(src_path + '/')} {shlex.quote(dest_path + '/')}".strip(),
            )

            log.info("Syncing: %s → %s", source, dest_path)
            r = self._run(rsync_cmd, timeout=7200)

            if r.ok:
                log.info("✓ %s synced (%.1fs)", source, r.duration_seconds)
                self.ctx.sources_synced += 1
                self._parse_rsync_stats(r)
            else:
                self.ctx.add_error(f"rsync failed for {source}: {r.stderr[:200]}")
                log.error(self.ctx.errors[-1])
                self.ctx.sources_failed += 1
                all_ok = False

        if use_snapshots:
            self._run(f"sudo ln -sfn {dest_base} {latest_link}")
            log.info("Updated latest → %s", snapshot_name)

        return all_ok

    @staticmethod
    def _parse_size_to_bytes(s: str) -> int:
        """Parse rsync size: raw '10,737,418,240' or human-readable '10.37G'."""
        s = s.strip()
        m = re.match(r"^([\d,.]+)\s*([KMGTP]?)$", s, re.IGNORECASE)
        if not m:
            return 0
        val = float(m.group(1).replace(",", ""))
        unit = m.group(2).upper()
        multipliers = {"": 1, "K": 1024, "M": 1024**2, "G": 1024**3, "T": 1024**4}
        return int(val * multipliers.get(unit, 1))

    def _parse_rsync_stats(self, result: CommandResult) -> None:
        combined = f"{result.stdout}\n{result.stderr}"

        m = re.search(r"Number of regular files transferred:\s*([\d,]+)", combined)
        if m:
            new_val = int(m.group(1).replace(",", ""))
            existing = int(self.ctx.rsync_stats.get("files_transferred", "0"))
            self.ctx.rsync_stats["files_transferred"] = str(existing + new_val)

        m = re.search(r"Total transferred file size:\s*([\d,.]+\s*[KMGTP]?)", combined)
        if m:
            new_val = self._parse_size_to_bytes(m.group(1))
            existing = int(self.ctx.rsync_stats.get("total_size", "0"))
            self.ctx.rsync_stats["total_size"] = str(existing + new_val)

    # ── Retention ────────────────────────────────────────────────────────

    def apply_retention(self) -> None:
        cfg = self.cfg
        if cfg.retention_keep <= 0:
            log.info("Retention: disabled (mirror mode)")
            return

        log.info("Applying retention: keep last %d snapshots", cfg.retention_keep)

        r = self._run(f"sudo ls -1d {cfg.retention_dir}/20[0-9][0-9]-* 2>/dev/null | sort")
        if not r.ok or not r.stdout.strip():
            log.info("No snapshots found")
            return

        snapshots = [s.strip() for s in r.stdout.strip().splitlines() if s.strip()]
        self.ctx.snapshots_kept = min(len(snapshots), cfg.retention_keep)

        if len(snapshots) <= cfg.retention_keep:
            log.info("Have %d snapshots (limit %d) — keeping all",
                     len(snapshots), cfg.retention_keep)
            return

        to_remove = snapshots[: len(snapshots) - cfg.retention_keep]
        to_keep = snapshots[len(snapshots) - cfg.retention_keep:]

        log.info("Removing %d old snapshots, keeping %d", len(to_remove), len(to_keep))

        for snap in to_remove:
            if not snap or ".." in snap:
                continue
            log.info("Removing: %s", snap)
            r = self._run(f"sudo rm -rf {snap}")
            if r.ok:
                self.ctx.snapshots_removed += 1
            else:
                self.ctx.add_error(f"Failed to remove {snap}: {r.stderr[:100]}")
                log.error(self.ctx.errors[-1])

        self.ctx.snapshots_kept = len(to_keep)
        log.info("Retention done: kept=%d removed=%d",
                 self.ctx.snapshots_kept, self.ctx.snapshots_removed)

    # ── Target Info ──────────────────────────────────────────────────────

    def get_disk_info(self) -> tuple[str, str]:
        dest = self.cfg.retention_dir if self.cfg.retention_keep > 0 else self.cfg.backup_dest
        r = self._run(f"df -h {dest} | tail -1")
        if r.ok:
            parts = r.stdout.split()
            if len(parts) >= 5:
                return parts[3], parts[4]
        return "unknown", "unknown"

    def get_backup_size(self) -> str:
        dest = self.cfg.retention_dir if self.cfg.retention_keep > 0 else self.cfg.backup_dest
        r = self._run(f"sudo du -sh {dest} 2>/dev/null | cut -f1")
        return r.stdout.strip() if r.ok else "unknown"

    def get_hostname(self) -> str:
        r = self._run("hostname")
        return r.stdout.strip() if r.ok else "backup-target"

    # ── Report ───────────────────────────────────────────────────────────

    def build_report(self) -> BackupReport:
        disk_free, disk_pct = self.get_disk_info()

        transferred = self.ctx.rsync_stats.get("total_size", "0")
        try:
            bval = int(transferred.replace(",", ""))
            if bval >= 1_073_741_824:
                transferred = f"{bval / 1_073_741_824:.2f} GB"
            elif bval >= 1_048_576:
                transferred = f"{bval / 1_048_576:.2f} MB"
            elif bval > 0:
                transferred = f"{bval / 1024:.1f} KB"
            else:
                transferred = "0 B"
        except ValueError:
            pass

        return BackupReport(
            hostname=self.get_hostname(),
            date=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            status=self.ctx.status,
            duration_human=format_duration(time.time() - self.ctx.start_time),
            transferred_size=transferred,
            total_backup_size=self.get_backup_size(),
            disk_free=disk_free,
            disk_used_pct=disk_pct,
            source=f"//{self.cfg.smb_ip}/{self.cfg.smb_share}",
            destination=self.cfg.backup_dest,
            snapshots_kept=self.ctx.snapshots_kept,
            snapshots_removed=self.ctx.snapshots_removed,
            errors=self.ctx.errors if self.ctx.errors else [],
        )

    # ── Full Pipeline ────────────────────────────────────────────────────

    def execute(self) -> BackupReport:
        log.info("=" * 60)
        log.info("COLD BACKUP — Starting")
        log.info("=" * 60)

        try:
            if not self.mount_smb():
                return self.build_report()

            self.run_rsync()
            self.unmount_smb()
            self.apply_retention()

        except Exception as e:
            self.ctx.add_error(f"Unexpected error: {e}")
            log.exception("Pipeline failed")
        finally:
            if self.ctx.smb_mounted:
                self.unmount_smb()

        report = self.build_report()
        log.info("=" * 60)
        log.info("COLD BACKUP — Finished: %s (%.0fs)",
                 report.status, time.time() - self.ctx.start_time)
        log.info("=" * 60)
        return report
