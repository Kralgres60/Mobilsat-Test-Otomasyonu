#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NETA DRX9000 - Enterprise Factory & Database Management Platform
Author: Semi Eren Gökyıldız
Copyright: (C) 2026 NETA Elektronik A.Ş.

Comprehensive multi-server, fleet monitoring, user management, and test analytics platform.
"""

import sys
import os
import json
import subprocess
import csv
import io
import socket
import time
import uuid
import urllib.parse
try:
    from http.server import ThreadingHTTPServer as HttpServerClass, BaseHTTPRequestHandler
except ImportError:
    from http.server import HTTPServer as HttpServerClass, BaseHTTPRequestHandler
from http.cookies import SimpleCookie

PORT = 8000
DB_HOST = "127.0.0.1"
DB_PORT = "3306"
DB_USER = "root"
DB_PASS = "neta123"
DB_NAME = "neta_antenna_test"

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
PORTABLE_DIR = os.path.abspath(os.path.join(BASE_DIR, ".."))
MYSQL_BIN = os.path.join(PORTABLE_DIR, "bin", "mysql.exe")

if not os.path.exists(MYSQL_BIN):
    MYSQL_BIN = "mysql"

# In-memory sessions: token -> {"username": str, "role": str, "full_name": str, "expires": float}
SESSIONS = {}

def cleanup_sessions():
    now = time.time()
    expired = [k for k, v in SESSIONS.items() if v.get("expires", 0) < now]
    for k in expired:
        del SESSIONS[k]

def get_session_user(handler):
    cleanup_sessions()
    cookie_header = handler.headers.get("Cookie", "")
    if not cookie_header:
        return None
    cookies = SimpleCookie()
    try:
        cookies.load(cookie_header)
        if "neta_session" in cookies:
            token = cookies["neta_session"].value
            if token in SESSIONS:
                session = SESSIONS[token]
                if session.get("expires", 0) > time.time():
                    return session
    except Exception:
        pass
    return None

def is_mariadb_alive(host=DB_HOST, port=DB_PORT, timeout=0.8):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect((host, int(port)))
        s.close()
        return True
    except Exception:
        return False

def ensure_mariadb_alive():
    """Auto-starts portable MariaDB engine in background if port 3306 is not open"""
    if DB_HOST not in ("127.0.0.1", "localhost"):
        return True
    if is_mariadb_alive():
        return True

    mysqld_exe = os.path.join(PORTABLE_DIR, "bin", "mysqld.exe")
    my_ini = os.path.join(PORTABLE_DIR, "my.ini")
    data_dir = os.path.join(PORTABLE_DIR, "data")

    if not os.path.exists(mysqld_exe):
        return False

    print(f"[BILGI] MariaDB portu ({DB_PORT}) kapali, tasinabilir motor arka planda baslatiliyor...")

    # First run: init db if needed
    if not os.path.exists(os.path.join(data_dir, "mysql")):
        init_bin = os.path.join(PORTABLE_DIR, "bin", "mysql_install_db.exe")
        if os.path.exists(init_bin):
            subprocess.run(
                [init_bin, f"--datadir={data_dir}", "--password=neta123", "--allow-remote-root-access", f"--port={DB_PORT}", "--silent"],
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
            )

    cmd = [mysqld_exe, f"--defaults-file={my_ini}", f"--datadir={data_dir}"]
    flags = 0
    if os.name == 'nt':
        flags = subprocess.CREATE_NO_WINDOW | getattr(subprocess, "DETACHED_PROCESS", 0x00000008)

    try:
        subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags)
    except Exception as e:
        print(f"[HATA] mysqld baslatilamadi: {e}")
        return False

    # Wait up to 10 seconds for it to open
    for _ in range(20):
        time.sleep(0.5)
        if is_mariadb_alive():
            print("[BASARILI] MariaDB motoru arka planda aktif oldu ve port dinleniyor!")
            return True

    return False

def run_sql(query, database=DB_NAME):
    """Executes an SQL query via mysql.exe and returns (columns, rows, error)"""
    if DB_HOST in ("127.0.0.1", "localhost") and not is_mariadb_alive():
        ensure_mariadb_alive()

    cmd = [
        MYSQL_BIN,
        f"-h{DB_HOST}",
        f"-P{DB_PORT}",
        f"-u{DB_USER}",
        f"-p{DB_PASS}",
        "--default-character-set=utf8mb4",
        "-B"  # Batch mode: tab-separated with column headers
    ]
    if database:
        cmd.extend([f"-D{database}"])
    cmd.extend(["-e", query])

    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
        )
        if proc.returncode != 0:
            err = proc.stderr.strip()
            err_lines = [l for l in err.splitlines() if "Using a password" not in l]
            return [], [], "\n".join(err_lines) or "Unknown SQL error"

        output = proc.stdout
        lines = output.splitlines()
        if not lines:
            return [], [], None

        reader = csv.reader(lines, delimiter='\t')
        rows = list(reader)
        if not rows:
            return [], [], None

        headers = rows[0]
        data = rows[1:]
        return headers, data, None
    except Exception as e:
        return [], [], str(e)

def log_audit(username, role, action):
    """Writes an audit log entry to system_logs table"""
    msg = f"Web Konsolu: {action} ({username} - {role})".replace("'", "''")
    run_sql(f"INSERT INTO system_logs (log_level, source, message) VALUES ('INFO', 'WEB_CONSOLE', '{msg}');")

def build_excel_xml(table_name, headers, rows):
    """Generates an Excel 2003 XML spreadsheet string (native .xls) with styled headers and typed data."""
    def escape_xml(s):
        if s is None:
            return ""
        return str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")

    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<?mso-application progid="Excel.Sheet"?>',
        '<Workbook xmlns="urn:schemas-microsoft-com:office:spreadsheet"',
        ' xmlns:o="urn:schemas-microsoft-com:office:office"',
        ' xmlns:x="urn:schemas-microsoft-com:office:excel"',
        ' xmlns:ss="urn:schemas-microsoft-com:office:spreadsheet"',
        ' xmlns:html="http://www.w3.org/TR/REC-html40">',
        ' <Styles>',
        '  <Style ss:ID="Default" ss:Name="Normal">',
        '   <Alignment ss:Vertical="Center"/>',
        '   <Font ss:FontName="Segoe UI" ss:Size="10" ss:Color="#000000"/>',
        '  </Style>',
        '  <Style ss:ID="HeaderStyle">',
        '   <Alignment ss:Horizontal="Center" ss:Vertical="Center"/>',
        '   <Borders>',
        '    <Border ss:Position="Bottom" ss:LineStyle="Continuous" ss:Weight="1" ss:Color="#10B981"/>',
        '   </Borders>',
        '   <Font ss:FontName="Segoe UI" ss:Size="11" ss:Bold="1" ss:Color="#FFFFFF"/>',
        '   <Interior ss:Color="#0A1F17" ss:Pattern="Solid"/>',
        '  </Style>',
        '  <Style ss:ID="PassStyle">',
        '   <Font ss:FontName="Segoe UI" ss:Size="10" ss:Bold="1" ss:Color="#0D5C3A"/>',
        '   <Interior ss:Color="#D1FAE5" ss:Pattern="Solid"/>',
        '  </Style>',
        '  <Style ss:ID="FailStyle">',
        '   <Font ss:FontName="Segoe UI" ss:Size="10" ss:Bold="1" ss:Color="#8B0000"/>',
        '   <Interior ss:Color="#FEE2E2" ss:Pattern="Solid"/>',
        '  </Style>',
        ' </Styles>',
        f' <Worksheet ss:Name="{escape_xml(table_name)[:31]}">',
        '  <Table ss:DefaultRowHeight="20">',
    ]

    # Header Row
    lines.append('   <Row ss:Height="24">')
    for h in headers:
        lines.append(f'    <Cell ss:StyleID="HeaderStyle"><Data ss:Type="String">{escape_xml(h)}</Data></Cell>')
    lines.append('   </Row>')

    # Data Rows
    for row in rows:
        lines.append('   <Row>')
        for idx, val in enumerate(row):
            str_val = "" if val is None else str(val)
            header_name = headers[idx].lower() if idx < len(headers) else ""

            # Check for pass/fail style
            style_attr = ''
            if str_val == "PASS":
                style_attr = ' ss:StyleID="PassStyle"'
            elif str_val == "FAIL":
                style_attr = ' ss:StyleID="FailStyle"'

            # Check if numeric
            is_num = False
            try:
                if str_val and not (len(str_val) > 1 and str_val.startswith('0') and not str_val.startswith('0.')):
                    _ = float(str_val)
                    is_num = True
            except ValueError:
                is_num = False

            if is_num and "sn" not in header_name and "barcode" not in header_name and "code" not in header_name and "no" not in header_name:
                lines.append(f'    <Cell{style_attr}><Data ss:Type="Number">{str_val}</Data></Cell>')
            else:
                lines.append(f'    <Cell{style_attr}><Data ss:Type="String">{escape_xml(str_val)}</Data></Cell>')
        lines.append('   </Row>')

    lines.extend([
        '  </Table>',
        ' </Worksheet>',
        '</Workbook>'
    ])
    return "\n".join(lines)

def safe_int(v, default=0):
    """Safely parses an integer from SQL results, handling NULL strings, empty strings and None."""
    if v is None:
        return default
    s = str(v).strip()
    if s == '' or s.upper() == 'NULL':
        return default
    try:
        return int(float(s))
    except (ValueError, TypeError):
        return default

def get_live_monitor_payload():
    """Compiles real-time station line monitoring data, mutex status, and production stats for active stations only."""
    # 1. Reference Lock
    _, r_lock, _ = run_sql("SELECT locked_by_station, current_channel_no, lock_acquired_at FROM reference_lock WHERE id = 1;")
    locked_station = safe_int(r_lock[0][0]) if r_lock and len(r_lock[0]) > 0 else 0
    lock_channel = safe_int(r_lock[0][1]) if r_lock and len(r_lock[0]) > 1 else 0
    lock_time = str(r_lock[0][2]) if r_lock and len(r_lock[0]) > 2 else ""

    # 2. Active Cluster nodes for stations (only real, non-expired stations)
    _, r_nodes, _ = run_sql(
        "SELECT id, node_name, ip_address, port, status, active_test_sn, details, last_seen "
        "FROM cluster_nodes "
        "WHERE node_type = 'STATION_CLIENT' AND status != 'OFFLINE' "
        "AND TIMESTAMPDIFF(SECOND, last_seen, NOW()) <= 90 "
        "ORDER BY port ASC, id ASC;"
    )

    stations_data = []
    if r_nodes:
        for idx, r in enumerate(r_nodes):
            name = str(r[1])
            ip = str(r[2])
            port_or_id = safe_int(r[3], 0)
            raw_status = str(r[4])
            active_sn = str(r[5] or "")
            if active_sn.upper() == "NULL":
                active_sn = ""

            st_id = port_or_id if port_or_id > 0 else (idx + 1)
            if "1" in name and st_id <= 0: st_id = 1
            elif "2" in name and st_id <= 0: st_id = 2
            elif "3" in name and st_id <= 0: st_id = 3

            _, r_st_cnt, _ = run_sql(f"SELECT COUNT(*), SUM(CASE WHEN overall_result = 'PASS' THEN 1 ELSE 0 END), SUM(CASE WHEN overall_result = 'FAIL' THEN 1 ELSE 0 END) FROM test_runs WHERE station_id = {st_id};")
            tot = safe_int(r_st_cnt[0][0]) if r_st_cnt and len(r_st_cnt[0]) > 0 else 0
            pas = safe_int(r_st_cnt[0][1]) if r_st_cnt and len(r_st_cnt[0]) > 1 else 0
            fai = safe_int(r_st_cnt[0][2]) if r_st_cnt and len(r_st_cnt[0]) > 2 else 0
            rate = round((pas / tot * 100), 1) if tot > 0 else 0.0

            _, r_last, _ = run_sql(f"SELECT antenna_sn, overall_result, avg_delta_db, duration_sec, test_timestamp FROM test_runs WHERE station_id = {st_id} ORDER BY id DESC LIMIT 1;")
            last_test = None
            if r_last and len(r_last) > 0:
                last_test = {
                    "sn": r_last[0][0],
                    "result": r_last[0][1],
                    "avg_delta": r_last[0][2],
                    "duration": r_last[0][3],
                    "timestamp": str(r_last[0][4])
                }

            # State determination
            if locked_station == st_id:
                state = "LOCKED"
                state_label = f"🔒 REF KİLİTLİ (Kanal {lock_channel})"
            elif active_sn or raw_status == "BUSY":
                state = "TESTING"
                state_label = "⚡ TEST EDİLİYOR"
            elif raw_status == "OFFLINE":
                state = "OFFLINE"
                state_label = "🔴 ÇEVRİMDIŞI"
            else:
                state = "IDLE"
                state_label = "🟢 BOŞTA / HAZIR"

            stations_data.append({
                "station_id": st_id,
                "station_name": name if name else f"İstasyon {st_id}",
                "ip": ip,
                "state": state,
                "state_label": state_label,
                "active_sn": active_sn,
                "total_tests": tot,
                "pass_tests": pas,
                "fail_tests": fai,
                "pass_rate": rate,
                "last_test": last_test
            })

    # 4. Global recent tests (last 8)
    _, r_rec, _ = run_sql("SELECT id, antenna_sn, station_id, tester_name, overall_result, avg_delta_db, duration_sec, test_timestamp FROM test_runs ORDER BY id DESC LIMIT 8;")
    recent_events = []
    if r_rec:
        for r in r_rec:
            recent_events.append({
                "id": r[0], "sn": r[1], "station": r[2], "tester": r[3],
                "result": r[4], "avg_delta": r[5], "duration": r[6], "timestamp": str(r[7])
            })

    return {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "lock": {
            "is_locked": locked_station > 0,
            "locked_station": locked_station,
            "channel_no": lock_channel,
            "acquired_at": lock_time
        },
        "stations": stations_data,
        "recent_events": recent_events
    }

def authenticate_user(username, password):
    """Verifies credentials against MariaDB users table or factory fallback"""
    u = username.strip()
    p = password.strip()
    if not u:
        return None

    u_sql = u.replace("'", "''")
    headers, rows, err = run_sql(
        f"SELECT role, full_name, password_hash, is_active FROM users WHERE username = '{u_sql}' AND is_active = 1 LIMIT 1;"
    )
    if not err and rows and len(rows) > 0:
        role = rows[0][0].upper()
        full_name = rows[0][1]
        pass_hash = rows[0][2]
        if pass_hash == p or pass_hash == "":
            return {
                "username": u,
                "role": role,
                "full_name": full_name if full_name else u
            }

    # Factory Fallbacks (matching DbManager in Qt desktop app)
    if u.lower() == "admin" and p == "123":
        return {"username": "admin", "role": "ADMIN", "full_name": "Sistem Yöneticisi / Başmühendis"}
    if u.lower() == "engineer" and p == "123":
        return {"username": "engineer", "role": "ADMIN", "full_name": "Ar-Ge Test Mühendisi"}
    if u.lower() == "operator" and p == "123":
        return {"username": "operator", "role": "OPERATOR", "full_name": "Hat Operatörü"}

    return None

def ensure_database():
    """Ensures neta_antenna_test database and initial schema exist"""
    _, _, err = run_sql("CREATE DATABASE IF NOT EXISTS `neta_antenna_test` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;", database=None)
    if err:
        print(f"[UYARI] Veritabani olusturma kontrolu: {err}")
        return

    headers, rows, err = run_sql("SHOW TABLES;", database=DB_NAME)
    if err or not rows or len(rows) == 0:
        print("[BILGI] 'neta_antenna_test' tablolari bulunamadi, sema ve tohum verileri otomatik yukleniyor...")
        possible_dirs = [
            os.path.abspath(os.path.join(PORTABLE_DIR, "sql")),
            os.path.abspath(os.path.join(PORTABLE_DIR, "..", "..", "sql")),
            os.path.abspath(os.path.join(PORTABLE_DIR, "..", "sql")),
            os.path.abspath(os.path.join(BASE_DIR, "sql")),
        ]
        schema_file = None
        seed_file = None
        for d in possible_dirs:
            sf = os.path.join(d, "schema_mysql.sql")
            sd = os.path.join(d, "seed_data.sql")
            if os.path.exists(sf) and not schema_file:
                schema_file = sf
            if os.path.exists(sd) and not seed_file:
                seed_file = sd

        if schema_file and os.path.exists(schema_file):
            print(f"      Sema yukleniyor: {schema_file}")
            with open(schema_file, "r", encoding="utf-8") as f:
                content = f.read()
            subprocess.run(
                [MYSQL_BIN, f"-h{DB_HOST}", f"-P{DB_PORT}", f"-u{DB_USER}", f"-p{DB_PASS}", "--default-character-set=utf8mb4"],
                input=content, text=True, encoding="utf-8",
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
            )

        if seed_file and os.path.exists(seed_file):
            print(f"      Seed verileri yukleniyor: {seed_file}")
            with open(seed_file, "r", encoding="utf-8") as f:
                content = f.read()
            subprocess.run(
                [MYSQL_BIN, f"-h{DB_HOST}", f"-P{DB_PORT}", f"-u{DB_USER}", f"-p{DB_PASS}", "--default-character-set=utf8mb4", f"-D{DB_NAME}"],
                input=content, text=True, encoding="utf-8",
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
            )
        print("[BASARILI] Sema ve test profilleri otomatik yuklendi!")

    # Check and create cluster_nodes table if missing
    run_sql("""
    CREATE TABLE IF NOT EXISTS `cluster_nodes` (
        `id` INT AUTO_INCREMENT PRIMARY KEY,
        `node_type` ENUM('HOST_SERVER', 'STATION_CLIENT', 'WEB_SERVICE') NOT NULL DEFAULT 'HOST_SERVER',
        `node_name` VARCHAR(100) NOT NULL,
        `ip_address` VARCHAR(45) NOT NULL,
        `port` INT NOT NULL DEFAULT 45454,
        `status` ENUM('ONLINE', 'BUSY', 'OFFLINE') NOT NULL DEFAULT 'ONLINE',
        `active_test_sn` VARCHAR(64) DEFAULT NULL,
        `details` VARCHAR(255) DEFAULT NULL,
        `last_seen` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
        UNIQUE KEY `uk_node_ip_port` (`node_type`, `ip_address`, `port`)
    ) ENGINE=InnoDB;
    """)

    # Remove legacy sample/dummy cluster nodes so only real active servers/stations are displayed
    run_sql("""
    DELETE FROM `cluster_nodes` WHERE `node_name` IN (
        'NETA-HOST-ANA-HAT', 'NETA-HOST-ARGE-02',
        'ISTASYON-MASA-1', 'ISTASYON-MASA-2', 'ISTASYON-MASA-3',
        'Ana Yönetici Sunucu (Host #1)', 'Yedek Kalite Kontrol Sunucusu (Host #2)',
        'İstasyon 1 (Test Masası #1)', 'İstasyon 2 (Test Masası #2)', 'İstasyon 3 (Test Masası #3)'
    );
    """)

    # Ensure system_settings descriptions are in simple A1-A2 English
    run_sql("""
    INSERT INTO `system_settings` (`setting_key`, `setting_value`, `description`) VALUES
        ('LOCK_TIMEOUT_SEC', '90', 'Safety timeout for reference device lock (seconds)'),
        ('SAMPLE_DURATION_SEC', '5', 'Signal measurement time per channel (seconds)'),
        ('DEFAULT_TOLERANCE_DB', '1.50', 'Default allowed Delta dB limit'),
        ('TCP_PORT', '9100', 'Main Host TCP port for commands and sync'),
        ('UDP_BEACON_PORT', '9101', 'Local network port for auto discovery'),
        ('REFERENCE_MODE', 'LIVE', 'Reference mode: LIVE (Live Antenna) or GOLDEN (Saved Golden Sample)'),
        ('ACTIVE_GOLDEN_RECORD_ID', '0', 'Active Golden Sample record ID selected by Admin'),
        ('REPORT_OUTPUT_DIR', 'reports', 'Folder path to save test reports')
    ON DUPLICATE KEY UPDATE `description` = VALUES(`description`);
    """)

HTML_PAGE = r"""<!DOCTYPE html>
<html lang="tr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Content-Security-Policy" content="default-src * 'unsafe-inline' 'unsafe-eval' data: blob:; script-src * 'unsafe-inline' 'unsafe-eval'; style-src * 'unsafe-inline';">
    <title>NETA DRX9000 - Kurumsal Test ve Filo Yönetim Paneli</title>
    <style>
        :root {
            --bg-dark: #071210;
            --bg-card: #0A1F17;
            --bg-hover: #123326;
            --border: #1A3D2B;
            --primary: #10B981;
            --primary-light: #34D399;
            --text-main: #E6F4EE;
            --text-muted: #8AAEA0;
            --fail-bg: #4B1515;
            --fail-text: #FF6B6B;
            --pass-bg: #0D3E28;
            --pass-text: #34D399;
            --warn-bg: #3F2E09;
            --warn-text: #FBBF24;
            --accent-blue: #38BDF8;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background-color: var(--bg-dark);
            color: var(--text-main);
            display: flex;
            height: 100vh;
            overflow: hidden;
        }
        /* Login Overlay */
        #loginOverlay {
            position: fixed;
            top: 0; left: 0; width: 100%; height: 100%;
            background-color: rgba(4, 10, 8, 0.96);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 9999;
            backdrop-filter: blur(8px);
        }
        .login-card {
            background-color: var(--bg-card);
            border: 1px solid var(--border);
            width: 380px;
            padding: 32px 28px;
            border-radius: 12px;
            box-shadow: 0 12px 36px rgba(0,0,0,0.6);
            display: flex;
            flex-direction: column;
            gap: 18px;
        }
        .login-header {
            text-align: center;
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 8px;
        }
        .login-badge {
            background-color: var(--primary);
            color: var(--bg-dark);
            font-weight: 900;
            padding: 6px 14px;
            border-radius: 6px;
            font-size: 16px;
            letter-spacing: 1.5px;
        }
        .login-title {
            font-size: 16px;
            font-weight: bold;
            color: var(--primary-light);
        }
        .login-sub {
            font-size: 12px;
            color: var(--text-muted);
        }
        .form-group {
            display: flex;
            flex-direction: column;
            gap: 6px;
        }
        .form-group label {
            font-size: 12px;
            font-weight: 600;
            color: var(--text-muted);
        }
        .form-control {
            background-color: var(--bg-dark);
            border: 1px solid var(--border);
            color: var(--text-main);
            padding: 10px 14px;
            border-radius: 6px;
            font-size: 13px;
        }
        .form-control:focus { outline: none; border-color: var(--primary-light); }
        .login-btn {
            background-color: var(--primary);
            color: var(--bg-dark);
            border: none;
            padding: 11px;
            border-radius: 6px;
            font-size: 13px;
            font-weight: bold;
            cursor: pointer;
            transition: 0.2s;
            margin-top: 6px;
        }
        .login-btn:hover { background-color: var(--primary-light); }
        .login-error {
            background-color: var(--fail-bg);
            color: var(--fail-text);
            border: 1px solid rgba(255,107,107,0.3);
            padding: 10px 12px;
            border-radius: 6px;
            font-size: 12px;
            display: none;
        }
        .login-hint {
            font-size: 11px;
            color: var(--text-muted);
            text-align: center;
            line-height: 1.4;
        }
        /* Sidebar */
        aside {
            width: 250px;
            background-color: var(--bg-card);
            border-right: 1px solid var(--border);
            display: flex;
            flex-direction: column;
            flex-shrink: 0;
        }
        .brand {
            padding: 18px 20px;
            border-bottom: 1px solid var(--border);
            display: flex;
            align-items: center;
            gap: 12px;
        }
        .brand-badge {
            background-color: var(--primary);
            color: var(--bg-dark);
            font-weight: 900;
            padding: 6px 10px;
            border-radius: 6px;
            font-size: 14px;
            letter-spacing: 1px;
        }
        .brand-title {
            font-size: 13px;
            font-weight: bold;
            color: var(--primary-light);
            line-height: 1.2;
        }
        .brand-sub {
            font-size: 10.5px;
            color: var(--text-muted);
        }
        .nav-section {
            padding: 14px 20px 6px;
            font-size: 10.5px;
            font-weight: bold;
            text-transform: uppercase;
            color: var(--text-muted);
            letter-spacing: 0.5px;
        }
        .nav-menu {
            list-style: none;
            overflow-y: auto;
            flex: 1;
            padding: 6px 10px;
        }
        .nav-item {
            padding: 10px 14px;
            margin-bottom: 4px;
            border-radius: 6px;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 10px;
            font-size: 13px;
            transition: all 0.2s;
            color: var(--text-muted);
        }
        .nav-item:hover {
            background-color: var(--bg-hover);
            color: var(--text-main);
        }
        .nav-item.active {
            background-color: var(--primary);
            color: var(--bg-dark);
            font-weight: bold;
        }
        .sidebar-footer {
            padding: 14px 16px;
            border-top: 1px solid var(--border);
            font-size: 11px;
            color: var(--text-muted);
            display: flex;
            flex-direction: column;
            gap: 4px;
        }
        .status-dot {
            display: inline-block;
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background-color: var(--primary-light);
            margin-right: 6px;
        }
        /* Main Container */
        main {
            flex: 1;
            display: flex;
            flex-direction: column;
            overflow: hidden;
            background-color: var(--bg-dark);
        }
        header {
            padding: 12px 24px;
            border-bottom: 1px solid var(--border);
            background-color: var(--bg-card);
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .header-title {
            font-size: 16px;
            font-weight: bold;
            color: var(--text-main);
            display: flex;
            align-items: center;
            gap: 8px;
        }
        .header-actions {
            display: flex;
            align-items: center;
            gap: 12px;
        }
        .user-pill {
            background-color: var(--bg-dark);
            border: 1px solid var(--border);
            padding: 5px 12px;
            border-radius: 20px;
            display: flex;
            align-items: center;
            gap: 8px;
            font-size: 12px;
        }
        .role-badge {
            background-color: var(--primary);
            color: var(--bg-dark);
            font-size: 10px;
            font-weight: 800;
            padding: 2px 6px;
            border-radius: 4px;
            text-transform: uppercase;
        }
        .btn {
            padding: 7px 14px;
            border-radius: 6px;
            border: none;
            font-size: 12px;
            font-weight: bold;
            cursor: pointer;
            display: inline-flex;
            align-items: center;
            gap: 6px;
            transition: 0.2s;
        }
        .btn-primary { background-color: var(--primary); color: var(--bg-dark); }
        .btn-primary:hover { background-color: var(--primary-light); }
        .btn-outline { background: none; border: 1px solid var(--border); color: var(--text-main); }
        .btn-outline:hover { background-color: var(--bg-hover); }
        .btn-danger { background-color: var(--fail-bg); color: var(--fail-text); border: 1px solid rgba(255,107,107,0.3); }
        .btn-danger:hover { background-color: #5C1A1A; }
        .btn-sm { padding: 4px 8px; font-size: 11px; }
        /* Content Area */
        .content-area {
            flex: 1;
            overflow-y: auto;
            padding: 22px 26px;
        }
        .page-view {
            display: none;
            flex-direction: column;
            gap: 20px;
        }
        .page-view.active {
            display: flex;
        }
        /* Metrics Grid */
        .metric-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 16px;
        }
        .metric-card {
            background-color: var(--bg-card);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 16px;
            display: flex;
            flex-direction: column;
            gap: 6px;
        }
        .metric-title {
            font-size: 11.5px;
            color: var(--text-muted);
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .metric-value {
            font-size: 26px;
            font-weight: bold;
            color: var(--text-main);
        }
        .metric-sub {
            font-size: 11px;
            color: var(--primary-light);
        }
        /* Cards */
        .card {
            background-color: var(--bg-card);
            border: 1px solid var(--border);
            border-radius: 8px;
            overflow: hidden;
            display: flex;
            flex-direction: column;
        }
        .card-header {
            padding: 14px 20px;
            border-bottom: 1px solid var(--border);
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-size: 13.5px;
            font-weight: bold;
            color: var(--primary-light);
        }
        .card-body {
            padding: 18px 20px;
        }
        /* Tables */
        .table-responsive {
            overflow-x: auto;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            font-size: 12.5px;
            text-align: left;
        }
        th {
            background-color: #0c251c;
            color: var(--primary-light);
            padding: 10px 14px;
            border-bottom: 1px solid var(--border);
            font-weight: 600;
            white-space: nowrap;
        }
        td {
            padding: 10px 14px;
            border-bottom: 1px solid rgba(26, 61, 43, 0.4);
            white-space: nowrap;
            color: var(--text-main);
        }
        tr:hover td {
            background-color: rgba(18, 51, 38, 0.35);
        }
        /* Badges */
        .badge {
            padding: 3px 8px;
            border-radius: 4px;
            font-size: 11px;
            font-weight: bold;
            text-transform: uppercase;
            display: inline-block;
        }
        .badge-pass { background-color: var(--pass-bg); color: var(--pass-text); border: 1px solid rgba(52,211,153,0.3); }
        .badge-fail { background-color: var(--fail-bg); color: var(--fail-text); border: 1px solid rgba(255,107,107,0.3); }
        .badge-online { background-color: var(--pass-bg); color: var(--pass-text); }
        .badge-offline { background-color: var(--fail-bg); color: var(--fail-text); }
        .badge-role { background-color: #1E293B; color: #94A3B8; }
        .badge-admin { background-color: #312E81; color: #A5B4FC; border: 1px solid rgba(165,180,252,0.3); }
        /* Modal Window */
        .modal {
            position: fixed;
            top: 0; left: 0; width: 100%; height: 100%;
            background-color: rgba(0,0,0,0.8);
            display: none;
            align-items: center;
            justify-content: center;
            z-index: 1000;
            backdrop-filter: blur(4px);
        }
        .modal.active { display: flex; }
        .modal-content {
            background-color: var(--bg-card);
            border: 1px solid var(--border);
            border-radius: 10px;
            width: 480px;
            max-width: 90%;
            max-height: 85vh;
            overflow-y: auto;
            box-shadow: 0 16px 40px rgba(0,0,0,0.7);
        }
        .modal-header {
            padding: 16px 20px;
            border-bottom: 1px solid var(--border);
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-size: 14px;
            font-weight: bold;
            color: var(--primary-light);
        }
        .modal-body {
            padding: 20px;
            display: flex;
            flex-direction: column;
            gap: 14px;
        }
        .modal-footer {
            padding: 14px 20px;
            border-top: 1px solid var(--border);
            display: flex;
            justify-content: flex-end;
            gap: 10px;
        }
        .close-btn {
            background: none;
            border: none;
            color: var(--text-muted);
            font-size: 18px;
            cursor: pointer;
        }
        .close-btn:hover { color: var(--fail-text); }
        /* Lock Banner */
        .lock-banner {
            background-color: #0c251c;
            border: 1px solid var(--border);
            padding: 12px 18px;
            border-radius: 8px;
            display: flex;
            align-items: center;
            justify-content: space-between;
        }
        /* Search Box */
        .filter-bar {
            display: flex;
            gap: 10px;
            margin-bottom: 14px;
            flex-wrap: wrap;
        }
        .filter-input {
            background-color: var(--bg-dark);
            border: 1px solid var(--border);
            color: var(--text-main);
            padding: 7px 12px;
            border-radius: 6px;
            font-size: 12.5px;
            min-width: 200px;
        }
        .filter-input:focus { outline: none; border-color: var(--primary-light); }

        /* Live Monitor & SSE Styles */
        @keyframes pulseGreen {
            0% { transform: scale(0.95); box-shadow: 0 0 0 0 rgba(52, 211, 153, 0.7); }
            70% { transform: scale(1); box-shadow: 0 0 0 8px rgba(52, 211, 153, 0); }
            100% { transform: scale(0.95); box-shadow: 0 0 0 0 rgba(52, 211, 153, 0); }
        }
        .live-pulse-dot {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            background-color: var(--primary-light);
            animation: pulseGreen 1.6s infinite ease-in-out;
            display: inline-block;
        }
        .station-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
            gap: 16px;
        }
        .station-card {
            background-color: var(--bg-card);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 18px;
            display: flex;
            flex-direction: column;
            gap: 14px;
            position: relative;
            transition: all 0.3s ease;
        }
        .station-card.locked {
            border-color: var(--warn-text);
            box-shadow: 0 0 12px rgba(251, 191, 36, 0.15);
        }
        .station-card.testing {
            border-color: var(--accent-blue);
            box-shadow: 0 0 12px rgba(56, 189, 248, 0.2);
        }
        .station-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid var(--border);
            padding-bottom: 10px;
        }
        .station-title {
            font-size: 14.5px;
            font-weight: bold;
            color: var(--text-main);
            display: flex;
            align-items: center;
            gap: 8px;
        }
        .station-ip {
            font-size: 11px;
            color: var(--text-muted);
            font-family: monospace;
        }
        .station-badge-idle {
            background-color: var(--pass-bg);
            color: var(--pass-text);
            border: 1px solid rgba(52, 211, 153, 0.3);
            font-size: 11px;
            font-weight: bold;
            padding: 3px 8px;
            border-radius: 4px;
        }
        .station-badge-testing {
            background-color: rgba(56, 189, 248, 0.15);
            color: var(--accent-blue);
            border: 1px solid rgba(56, 189, 248, 0.4);
            font-size: 11px;
            font-weight: bold;
            padding: 3px 8px;
            border-radius: 4px;
            animation: pulseTesting 1.2s infinite alternate;
        }
        @keyframes pulseTesting {
            from { opacity: 0.8; }
            to { opacity: 1.0; }
        }
        .station-badge-locked {
            background-color: var(--warn-bg);
            color: var(--warn-text);
            border: 1px solid rgba(251, 191, 36, 0.3);
            font-size: 11px;
            font-weight: bold;
            padding: 3px 8px;
            border-radius: 4px;
        }
        .station-badge-offline {
            background-color: var(--fail-bg);
            color: var(--fail-text);
            border: 1px solid rgba(255, 107, 107, 0.3);
            font-size: 11px;
            font-weight: bold;
            padding: 3px 8px;
            border-radius: 4px;
        }
        .active-sn-banner {
            background-color: rgba(56, 189, 248, 0.08);
            border: 1px dashed rgba(56, 189, 248, 0.4);
            border-radius: 6px;
            padding: 10px 12px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .stat-mini-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 8px;
            background-color: var(--bg-dark);
            border-radius: 6px;
            padding: 10px;
            text-align: center;
        }
        .stat-mini-val {
            font-size: 15px;
            font-weight: bold;
        }
        .stat-mini-lbl {
            font-size: 10px;
            color: var(--text-muted);
            margin-top: 2px;
        }
        .last-test-box {
            background-color: var(--bg-dark);
            border-radius: 6px;
            padding: 10px 12px;
            font-size: 12px;
            display: flex;
            flex-direction: column;
            gap: 4px;
        }
    </style>
</head>
<body>
    <!-- Login Modal -->
    <div id="loginOverlay">
        <div class="login-card">
            <div class="login-header">
                <span class="login-badge">NETA</span>
                <div class="login-title">DRX9000 KURUMSAL YÖNETİM</div>
                <div class="login-sub">Güvenli Oturum Açma</div>
            </div>
            <div id="loginError" class="login-error"></div>
            <form id="loginForm" onsubmit="event.preventDefault(); handleLogin(event);" style="display:flex; flex-direction:column; gap:14px;">
                <div class="form-group">
                    <label>Kullanıcı Adı</label>
                    <input type="text" id="loginUsername" class="form-control" placeholder="Örn: admin" required autofocus>
                </div>
                <div class="form-group">
                    <label>Şifre</label>
                    <input type="password" id="loginPassword" class="form-control" placeholder="••••" required>
                </div>
                <button type="button" id="loginBtn" class="login-btn" onclick="handleLogin(event)">🔑 Giriş Yap</button>
            </form>
            <div class="login-hint">
                Masaüstü uygulaması ile ortak kullanıcı veri tabanı (<code>users</code>) kullanılmaktadır.<br/>
                <span style="color:var(--primary-light);">Varsayılan: admin / 123</span>
            </div>
        </div>
    </div>

    <!-- Sidebar Navigation -->
    <aside>
        <div class="brand">
            <span class="brand-badge">NETA</span>
            <div>
                <div class="brand-title">DRX9000 FLEET</div>
                <div class="brand-sub">Test & Filo Yönetimi</div>
            </div>
        </div>
        <div class="nav-section">Modüller</div>
        <ul class="nav-menu" id="sidebarNav">
            <li class="nav-item active" data-page="dashboard" onclick="switchPage('dashboard')">📊 Genel Panel</li>
            <li class="nav-item" data-page="servers" onclick="switchPage('servers')">🖥️ Sunucular & Donanım</li>
            <li class="nav-item" data-page="users" onclick="switchPage('users')">👥 Kullanıcı Yönetimi</li>
            <li class="nav-item" data-page="tests" onclick="switchPage('tests')">📋 Test Geçmişi</li>
            <li class="nav-item" data-page="orders" onclick="switchPage('orders')">📦 İş Emirleri</li>
            <li class="nav-item" data-page="profiles" onclick="switchPage('profiles')">🛰️ Uydu Profilleri</li>
            <li class="nav-item" data-page="database" onclick="switchPage('database')">🗄️ Veritabanı & SQL</li>
            <li class="nav-item" data-page="logs" onclick="switchPage('logs')">📜 Denetim İzi (Audit)</li>
        </ul>
        <div class="sidebar-footer">
            <div><span class="status-dot"></span>MariaDB: <b>127.0.0.1:3306</b></div>
            <div>Veritabanı: <b>neta_antenna_test</b></div>
            <div>Geliştirici: <b>Semi Eren Gökyıldız</b></div>
        </div>
    </aside>

    <!-- Main Container -->
    <main>
        <header>
            <div class="header-title" id="pageHeaderTitle">📊 Genel Panel</div>
            <div class="header-actions">
                <button class="btn btn-outline" onclick="refreshCurrentPage()">🔄 Yenile</button>
                <div class="user-pill" id="userPill">
                    <span id="userFullName">Yükleniyor</span>
                    <span class="role-badge" id="userRoleBadge">ROLE</span>
                </div>
                <button class="btn btn-danger" onclick="handleLogout()" title="Oturumu Kapat">🚪 Çıkış</button>
            </div>
        </header>

        <div class="content-area">
            <!-- 1. DASHBOARD PAGE -->
            <div id="page-dashboard" class="page-view active">
                <div class="lock-banner" id="dashLockBanner">
                    <div>
                        <span style="font-size:14px; font-weight:bold;">🔒 Donanım Kilit Hakemi (Mutex):</span>
                        <span id="lockStatusText" style="margin-left:8px; color:var(--primary-light);">Yükleniyor...</span>
                    </div>
                    <span id="lockTimeText" style="font-size:11.5px; color:var(--text-muted);"></span>
                </div>

                <div class="metric-grid">
                    <div class="metric-card">
                        <span class="metric-title">Toplam Test</span>
                        <span class="metric-value" id="dashTotalTests">0</span>
                        <span class="metric-sub">Kayıtlı Anten Koşusu</span>
                    </div>
                    <div class="metric-card">
                        <span class="metric-title">Başarılı (PASS)</span>
                        <span class="metric-value" id="dashPassTests" style="color:var(--primary-light);">0</span>
                        <span class="metric-sub" id="dashPassRate">%0 Başarı Oranı</span>
                    </div>
                    <div class="metric-card">
                        <span class="metric-title">Hatalı (FAIL)</span>
                        <span class="metric-value" id="dashFailTests" style="color:var(--fail-text);">0</span>
                        <span class="metric-sub">Tolerans Dışı</span>
                    </div>
                    <div class="metric-card">
                        <span class="metric-title">Aktif Sunucular</span>
                        <span class="metric-value" id="dashActiveServers" style="color:var(--accent-blue);">0</span>
                        <span class="metric-sub">Çevrimiçi Yönetici PC</span>
                    </div>
                </div>

                <div class="card">
                    <div class="card-header">
                        <span>📋 Son Yapılan Test Koşuları</span>
                        <button class="btn btn-outline btn-sm" onclick="switchPage('tests')">Tümünü Gör</button>
                    </div>
                    <div class="table-responsive">
                        <table>
                            <thead>
                                <tr>
                                    <th>Anten Seri No</th>
                                    <th>İstasyon</th>
                                    <th>Test Yapan</th>
                                    <th>Ort. Delta (dB)</th>
                                    <th>Sonuç</th>
                                    <th>Tarih</th>
                                </tr>
                            </thead>
                            <tbody id="dashRecentTestsBody">
                                <tr><td colspan="6" style="color:var(--text-muted);">Yükleniyor...</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>

            <!-- 2. SERVERS & FLEET PAGE -->
            <div id="page-servers" class="page-view">
                <div class="card">
                    <div class="card-header">
                        <span>🖥️ Aktif Yönetici Sunucuları (Host Servers)</span>
                        <span style="font-size:11px; color:var(--text-muted);">Ağdaki çoklu yönetici host listesi</span>
                    </div>
                    <div class="table-responsive">
                        <table>
                            <thead>
                                <tr>
                                    <th>Sunucu Adı</th>
                                    <th>IP Adresi</th>
                                    <th>Port</th>
                                    <th>Mod / Açıklama</th>
                                    <th>Durum</th>
                                    <th>Son Görülme</th>
                                </tr>
                            </thead>
                            <tbody id="serversHostTableBody">
                                <tr><td colspan="6" style="color:var(--text-muted);">Yükleniyor...</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>

                <div class="card">
                    <div class="card-header">
                        <span>📡 Test İstasyonları Filosu (Stations)</span>
                        <span style="font-size:11px; color:var(--text-muted);">Yalnızca çevrimiçi ve aktif olan istasyonlar listelenir</span>
                    </div>
                    <div class="table-responsive">
                        <table>
                            <thead>
                                <tr>
                                    <th>İstasyon</th>
                                    <th>IP Adresi</th>
                                    <th>Aktif Test Anteni</th>
                                    <th>Açıklama</th>
                                    <th>Durum</th>
                                    <th>Son Sinyal</th>
                                </tr>
                            </thead>
                            <tbody id="serversStationTableBody">
                                <tr><td colspan="6" style="color:var(--text-muted);">Yükleniyor...</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>

            <!-- 3. USERS PAGE -->
            <div id="page-users" class="page-view">
                <div class="card">
                    <div class="card-header">
                        <span>👥 Kullanıcı ve Yetki Yönetimi</span>
                        <button class="btn btn-primary btn-sm" onclick="openCreateUserModal()">➕ Yeni Kullanıcı Ekle</button>
                    </div>
                    <div class="table-responsive">
                        <table>
                            <thead>
                                <tr>
                                    <th>ID</th>
                                    <th>Kullanıcı Adı</th>
                                    <th>Ad Soyad</th>
                                    <th>Rol</th>
                                    <th>Durum</th>
                                    <th>Oluşturulma Tarihi</th>
                                    <th>İşlemler</th>
                                </tr>
                            </thead>
                            <tbody id="usersTableBody">
                                <tr><td colspan="7" style="color:var(--text-muted);">Yükleniyor...</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>

            <!-- 4. TESTS PAGE -->
            <div id="page-tests" class="page-view">
                <div class="card">
                    <div class="card-header">
                        <span>📋 Anten Üretim Test Kayıtları</span>
                        <div style="display:flex; gap:8px;">
                            <button class="btn btn-outline btn-sm" onclick="exportCsv('test_runs')">📥 CSV İndir</button>
                            <button class="btn btn-outline btn-sm" onclick="exportExcel('test_runs')">📊 Excel İndir</button>
                        </div>
                    </div>
                    <div class="card-body" style="padding-bottom:6px;">
                        <div class="filter-bar">
                            <input type="text" id="testSearchInput" class="filter-input" placeholder="Anten Seri No veya Operatör ara..." onkeyup="filterTests()">
                            <select id="testResultFilter" class="filter-input" style="min-width:140px;" onchange="filterTests()">
                                <option value="">Tüm Sonuçlar</option>
                                <option value="PASS">Yalnızca PASS</option>
                                <option value="FAIL">Yalnızca FAIL</option>
                            </select>
                        </div>
                    </div>
                    <div class="table-responsive">
                        <table>
                            <thead>
                                <tr>
                                    <th>ID</th>
                                    <th>Anten Seri No</th>
                                    <th>İstasyon</th>
                                    <th>Operatör</th>
                                    <th>Sonuç</th>
                                    <th>Ort. Delta (dB)</th>
                                    <th>Süre (sn)</th>
                                    <th>Test Zamanı</th>
                                    <th>Detay</th>
                                </tr>
                            </thead>
                            <tbody id="testsTableBody">
                                <tr><td colspan="9" style="color:var(--text-muted);">Yükleniyor...</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>

            <!-- 5. WORK ORDERS PAGE -->
            <div id="page-orders" class="page-view">
                <div class="card">
                    <div class="card-header">
                        <span>📦 Üretim Siparişleri ve İş Emirleri</span>
                        <div style="display:flex; gap:8px;">
                            <button class="btn btn-outline btn-sm" onclick="exportCsv('work_orders')">📥 CSV İndir</button>
                            <button class="btn btn-outline btn-sm" onclick="exportExcel('work_orders')">📊 Excel İndir</button>
                            <button class="btn btn-primary btn-sm" onclick="openCreateOrderModal()">➕ Yeni İş Emri</button>
                        </div>
                    </div>
                    <div class="table-responsive">
                        <table>
                            <thead>
                                <tr>
                                    <th>İş Emri No</th>
                                    <th>Müşteri</th>
                                    <th>Ürün Kodu</th>
                                    <th>Hedef Miktar</th>
                                    <th>Tamamlanan</th>
                                    <th>PASS</th>
                                    <th>FAIL</th>
                                    <th>Durum</th>
                                    <th>Oluşturulma</th>
                                </tr>
                            </thead>
                            <tbody id="ordersTableBody">
                                <tr><td colspan="9" style="color:var(--text-muted);">Yükleniyor...</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>

            <!-- 6. SATELLITE PROFILES PAGE -->
            <div id="page-profiles" class="page-view">
                <div class="card">
                    <div class="card-header" style="flex-wrap:wrap; gap:10px;">
                        <span>🛰️ Uydu Test Profilleri ve RF Toleransları</span>
                        <button class="btn btn-primary btn-sm" onclick="openCreateProfileModal()">➕ Yeni Uydu Profili Ekle</button>
                    </div>
                    <div class="table-responsive">
                        <table>
                            <thead>
                                <tr>
                                    <th>Profil ID</th>
                                    <th>Profil Adı</th>
                                    <th>Uydu Adı</th>
                                    <th>Yörünge Pozisyonu</th>
                                    <th>Varsayılan Tolerans</th>
                                    <th>Durum</th>
                                    <th>İşlemler</th>
                                </tr>
                            </thead>
                            <tbody id="profilesTableBody">
                                <tr><td colspan="7" style="color:var(--text-muted);">Yükleniyor...</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>

                <div class="card">
                    <div class="card-header" style="flex-wrap:wrap; gap:10px; align-items:center;">
                        <span id="selectedProfileTitle">📡 Seçili Profil Frekans & Kanal Matrisi</span>
                        <div style="display:flex; gap:8px; align-items:center; margin-left:auto;">
                            <label style="font-size:12px; color:var(--text-muted);">Aktif Profil:</label>
                            <select id="profileSelectDropdown" class="form-control" style="width:auto; padding:5px 10px; font-size:12px;" onchange="onProfileDropdownChange()">
                            </select>
                            <button class="btn btn-primary btn-sm" onclick="openAddChannelModal()">➕ Yeni Frekans / Kanal Ekle</button>
                        </div>
                    </div>
                    <div class="table-responsive">
                        <table>
                            <thead>
                                <tr>
                                    <th>Kanal No</th>
                                    <th>Kanal Adı</th>
                                    <th>Frekans (MHz)</th>
                                    <th>Sembol Oranı</th>
                                    <th>Pol</th>
                                    <th>Tolerans (dB)</th>
                                    <th>İşlemler</th>
                                </tr>
                            </thead>
                            <tbody id="profileChannelsTableBody">
                                <tr><td colspan="7" style="color:var(--text-muted);">Yükleniyor...</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>

            <!-- 7. DATABASE & RAW SQL PAGE -->
            <div id="page-database" class="page-view">
                <div style="display:flex; gap:16px; height:600px;">
                    <!-- Left Mini Table Picker -->
                    <div class="card" style="width:240px; flex-shrink:0;">
                        <div class="card-header" style="font-size:12px;">Tablo Listesi</div>
                        <ul class="nav-menu" id="dbTableList" style="padding:4px 6px;">
                            <li class="nav-item">Yükleniyor...</li>
                        </ul>
                    </div>
                    <!-- Right Table & SQL View -->
                    <div style="flex:1; display:flex; flex-direction:column; gap:14px; overflow:hidden;">
                        <div style="display:flex; gap:8px;">
                            <button class="btn btn-primary btn-sm" id="btnDbViewTable" onclick="toggleDbSubTab('table')">📋 Tablo Verileri</button>
                            <button class="btn btn-outline btn-sm" id="btnDbViewSql" onclick="toggleDbSubTab('sql')">⚡ SQL Konsolu</button>
                            <div style="margin-left:auto; display:flex; gap:8px;">
                                <button class="btn btn-outline btn-sm" onclick="exportCsv(currentRawTable)">📥 CSV İndir</button>
                                <button class="btn btn-outline btn-sm" onclick="exportExcel(currentRawTable)">📊 Excel İndir</button>
                                <button class="btn btn-outline btn-sm" onclick="loadRawTable(currentRawTable)">🔄 Yenile</button>
                            </div>
                        </div>

                        <div id="rawTableView" class="card" style="flex:1;">
                            <div class="card-header" style="font-size:12px;">
                                <span id="rawTableNameTitle">Tablo Seçiniz</span>
                                <span id="rawRecordCount" style="font-size:11px; color:var(--text-muted);">0 Kayıt</span>
                            </div>
                            <div class="table-responsive" style="flex:1;">
                                <table id="rawDataTable">
                                    <thead><tr id="rawTableHead"></tr></thead>
                                    <tbody id="rawTableBody"></tbody>
                                </table>
                            </div>
                        </div>

                        <div id="rawSqlView" class="card" style="flex:1; display:none; padding:14px; gap:12px;">
                            <textarea id="sqlInput" class="form-control" style="height:120px; font-family:monospace; color:#34D399; font-size:13px;" placeholder="SQL sorgunuzu buraya yazın (Örn: SELECT * FROM test_runs LIMIT 20;)"></textarea>
                            <div>
                                <button class="btn btn-primary btn-sm" onclick="runCustomSql()">🚀 Sorguyu Çalıştır (F9)</button>
                            </div>
                            <div id="sqlStatus"></div>
                            <div class="table-responsive" style="flex:1;">
                                <table>
                                    <thead><tr id="sqlHead"></tr></thead>
                                    <tbody id="sqlBody"></tbody>
                                </table>
                            </div>
                        </div>
                    </div>
                </div>
            </div>

            <!-- 8. AUDIT LOGS PAGE -->
            <div id="page-logs" class="page-view">
                <div class="card">
                    <div class="card-header">
                        <span>📜 Sistem Güvenlik ve Denetim İzi (Audit Log)</span>
                        <span style="font-size:11px; color:var(--text-muted);">Son 100 işlem günlüğü</span>
                    </div>
                    <div class="table-responsive">
                        <table>
                            <thead>
                                <tr>
                                    <th>ID</th>
                                    <th>Seviye</th>
                                    <th>Kaynak</th>
                                    <th>Mesaj</th>
                                    <th>Zaman Damgası</th>
                                </tr>
                            </thead>
                            <tbody id="auditLogsTableBody">
                                <tr><td colspan="5" style="color:var(--text-muted);">Yükleniyor...</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>
        </div>
    </main>

    <!-- Modal: Create User -->
    <div id="modalCreateUser" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <span>➕ Yeni Kullanıcı Ekle</span>
                <button class="close-btn" onclick="closeModal('modalCreateUser')">&times;</button>
            </div>
            <form onsubmit="handleCreateUser(event)">
                <div class="modal-body">
                    <div class="form-group">
                        <label>Kullanıcı Adı</label>
                        <input type="text" id="newUsername" class="form-control" required placeholder="Örn: mehmet.kaya">
                    </div>
                    <div class="form-group">
                        <label>Ad Soyad</label>
                        <input type="text" id="newFullName" class="form-control" required placeholder="Örn: Mehmet Kaya">
                    </div>
                    <div class="form-group">
                        <label>Başlangıç Şifresi</label>
                        <input type="password" id="newPassword" class="form-control" required placeholder="••••">
                    </div>
                    <div class="form-group">
                        <label>Kullanıcı Rolü</label>
                        <select id="newRole" class="form-control">
                            <option value="OPERATOR">OPERATOR (Test Operatörü)</option>
                            <option value="ADMIN">ADMIN (Test Mühendisi / Yönetici)</option>
                        </select>
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-outline" onclick="closeModal('modalCreateUser')">İptal</button>
                    <button type="submit" class="btn btn-primary">Kaydet</button>
                </div>
            </form>
        </div>
    </div>

    <!-- Modal: Edit User -->
    <div id="modalEditUser" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <span>✏️ Kullanıcı Bilgilerini Düzenle</span>
                <button class="close-btn" onclick="closeModal('modalEditUser')">&times;</button>
            </div>
            <form onsubmit="handleEditUser(event)">
                <div class="modal-body">
                    <input type="hidden" id="editUserId">
                    <div class="form-group">
                        <label>Kullanıcı Adı</label>
                        <input type="text" id="editUsername" class="form-control" required>
                    </div>
                    <div class="form-group">
                        <label>Ad Soyad</label>
                        <input type="text" id="editFullName" class="form-control" required>
                    </div>
                    <div class="form-group">
                        <label>Kullanıcı Rolü</label>
                        <select id="editRole" class="form-control">
                            <option value="OPERATOR">OPERATOR (Test Operatörü)</option>
                            <option value="ADMIN">ADMIN (Test Mühendisi / Yönetici)</option>
                        </select>
                    </div>
                    <div class="form-group">
                        <label>Hesap Durumu</label>
                        <select id="editIsActive" class="form-control">
                            <option value="1">Aktif</option>
                            <option value="0">Pasif</option>
                        </select>
                    </div>
                    <div class="form-group">
                        <label>Yeni Şifre (İsteğe Bağlı)</label>
                        <input type="password" id="editPassword" class="form-control" placeholder="Boş bırakırsanız mevcut şifre korunur">
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-outline" onclick="closeModal('modalEditUser')">İptal</button>
                    <button type="submit" class="btn btn-primary">Değişiklikleri Kaydet</button>
                </div>
            </form>
        </div>
    </div>

    <!-- Modal: Update Password -->
    <div id="modalUpdatePassword" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <span>🔑 Şifre Değiştir</span>
                <button class="close-btn" onclick="closeModal('modalUpdatePassword')">&times;</button>
            </div>
            <form onsubmit="handleUpdatePassword(event)">
                <div class="modal-body">
                    <input type="hidden" id="passUserId">
                    <div class="form-group">
                        <label>Kullanıcı</label>
                        <input type="text" id="passUsername" class="form-control" disabled>
                    </div>
                    <div class="form-group">
                        <label>Yeni Şifre</label>
                        <input type="password" id="passNewPassword" class="form-control" required placeholder="Yeni şifreyi girin">
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-outline" onclick="closeModal('modalUpdatePassword')">İptal</button>
                    <button type="submit" class="btn btn-primary">Şifreyi Güncelle</button>
                </div>
            </form>
        </div>
    </div>

    <!-- Modal: Test Run Details (10 Channels) -->
    <div id="modalTestDetail" class="modal">
        <div class="modal-content" style="width:750px;">
            <div class="modal-header">
                <span id="testDetailTitle">🔍 Test Detayı</span>
                <button class="close-btn" onclick="closeModal('modalTestDetail')">&times;</button>
            </div>
            <div class="modal-body">
                <div class="table-responsive">
                    <table>
                        <thead>
                            <tr>
                                <th>Kanal</th>
                                <th>Kanal Adı</th>
                                <th>Frekans</th>
                                <th>DUT C/N</th>
                                <th>Ref C/N</th>
                                <th>Delta</th>
                                <th>Kilitler (S/V/A)</th>
                                <th>Sonuç</th>
                            </tr>
                        </thead>
                        <tbody id="testDetailChannelsBody"></tbody>
                    </table>
                </div>
            </div>
            <div class="modal-footer">
                <button type="button" class="btn btn-outline" onclick="closeModal('modalTestDetail')">Kapat</button>
            </div>
        </div>
    </div>

    <!-- Modal: Create Work Order -->
    <div id="modalCreateOrder" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <span>📦 Yeni İş Emri Oluştur</span>
                <button class="close-btn" onclick="closeModal('modalCreateOrder')">&times;</button>
            </div>
            <form onsubmit="handleCreateOrder(event)">
                <div class="modal-body">
                    <div class="form-group">
                        <label>İş Emri Numarası</label>
                        <input type="text" id="newOrderNo" class="form-control" required placeholder="Örn: NETA-2026-002">
                    </div>
                    <div class="form-group">
                        <label>Müşteri / Bölüm Adı</label>
                        <input type="text" id="newOrderCustomer" class="form-control" required placeholder="Örn: Fabrika Seri Üretim">
                    </div>
                    <div class="form-group">
                        <label>Ürün Kodu</label>
                        <input type="text" id="newOrderProduct" class="form-control" required placeholder="Örn: DRX9000-ANT-V1">
                    </div>
                    <div class="form-group">
                        <label>Hedef Anten Adedi</label>
                        <input type="number" id="newOrderQuantity" class="form-control" required min="1" value="100">
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-outline" onclick="closeModal('modalCreateOrder')">İptal</button>
                    <button type="submit" class="btn btn-primary">Oluştur</button>
                </div>
            </form>
        </div>
    </div>

    <!-- Modal: Create Satellite Profile -->
    <div id="modalCreateProfile" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <span>➕ Yeni Uydu Test Profili Ekle</span>
                <button class="close-btn" onclick="closeModal('modalCreateProfile')">&times;</button>
            </div>
            <form onsubmit="handleCreateProfile(event)">
                <div class="modal-body">
                    <div class="form-group">
                        <label>Profil Adı</label>
                        <input type="text" id="newProfName" class="form-control" required placeholder="Örn: Türksat 42.0E Standart">
                    </div>
                    <div class="form-group">
                        <label>Uydu Adı</label>
                        <input type="text" id="newProfSatellite" class="form-control" required placeholder="Örn: Türksat 4A/5B">
                    </div>
                    <div class="form-group">
                        <label>Yörünge Pozisyonu</label>
                        <input type="text" id="newProfOrbital" class="form-control" required value="42.0E" placeholder="Örn: 42.0E, 13.0E">
                    </div>
                    <div class="form-group">
                        <label>Varsayılan RF Toleransı (± dB)</label>
                        <input type="number" step="0.1" id="newProfTolerance" class="form-control" required value="1.50">
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-outline" onclick="closeModal('modalCreateProfile')">İptal</button>
                    <button type="submit" class="btn btn-primary">Kaydet</button>
                </div>
            </form>
        </div>
    </div>

    <!-- Modal: Add / Edit Channel Frequency -->
    <div id="modalAddChannel" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <span id="modalAddChannelTitle">➕ Profile Yeni Frekans / Kanal Ekle</span>
                <button class="close-btn" onclick="closeModal('modalAddChannel')">&times;</button>
            </div>
            <form onsubmit="handleAddChannel(event)">
                <div class="modal-body">
                    <div class="form-group">
                        <label>Hedef Profil</label>
                        <input type="text" id="channelTargetProfileName" class="form-control" disabled>
                    </div>
                    <div class="form-group">
                        <label>Kanal Numarası (1 .. 50)</label>
                        <input type="number" min="1" max="50" id="chNum" class="form-control" required placeholder="1">
                    </div>
                    <div class="form-group">
                        <label>Kanal / Transponder Adı</label>
                        <input type="text" id="chName" class="form-control" required placeholder="Örn: TRT 1 HD, ATV HD">
                    </div>
                    <div class="form-group">
                        <label>Frekans (MHz)</label>
                        <input type="number" id="chFreq" class="form-control" required placeholder="Örn: 11054">
                    </div>
                    <div class="form-group">
                        <label>Sembol Oranı (Symbol Rate - kS/s)</label>
                        <input type="number" id="chSr" class="form-control" required placeholder="Örn: 30000">
                    </div>
                    <div class="form-group">
                        <label>Polarizasyon</label>
                        <select id="chPol" class="form-control" required>
                            <option value="V">V (Dikey - Vertical)</option>
                            <option value="H">H (Yatay - Horizontal)</option>
                        </select>
                    </div>
                    <div class="form-group">
                        <label>Ölçüm Toleransı (± dB)</label>
                        <input type="number" step="0.1" id="chTol" class="form-control" required value="1.50">
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-outline" onclick="closeModal('modalAddChannel')">İptal</button>
                    <button type="submit" class="btn btn-primary">Kaydet</button>
                </div>
            </form>
        </div>
    </div>

    <script>
        let currentUser = null;
        let currentPage = 'dashboard';
        let currentRawTable = 'test_runs';
        let allTestsCache = [];

        // Auth
        async function checkAuth() {
            try {
                const res = await fetch('/api/me');
                if (res.ok) {
                    const data = await res.json();
                    if (data.authenticated && data.user) {
                        currentUser = data.user;
                        onLoginSuccess(currentUser);
                        return;
                    }
                }
                showLoginOverlay();
            } catch (err) {
                showLoginOverlay();
            }
        }

        function showLoginOverlay() {
            document.getElementById('loginOverlay').style.display = 'flex';
            document.getElementById('loginUsername').focus();
        }

        function onLoginSuccess(user) {
            document.getElementById('loginOverlay').style.display = 'none';
            document.getElementById('userFullName').textContent = user.full_name || user.username;
            document.getElementById('userRoleBadge').textContent = user.role;
            if (user.role === 'ADMIN') {
                document.getElementById('userRoleBadge').className = 'role-badge badge-admin';
            }
            switchPage('dashboard');
        }

        async function handleLogin(e) {
            if (e && e.preventDefault) e.preventDefault();
            const uInput = document.getElementById('loginUsername');
            const pInput = document.getElementById('loginPassword');
            const u = uInput ? uInput.value.trim() : '';
            const p = pInput ? pInput.value.trim() : '';
            const errDiv = document.getElementById('loginError');
            if (errDiv) errDiv.style.display = 'none';

            if (!u || !p) {
                if (errDiv) {
                    errDiv.textContent = 'Lütfen kullanıcı adı ve şifre giriniz!';
                    errDiv.style.display = 'block';
                }
                return;
            }

            const submitBtn = document.getElementById('loginBtn');
            if (submitBtn) {
                submitBtn.disabled = true;
                submitBtn.textContent = '⏳ Giriş yapılıyor...';
            }

            try {
                const res = await fetch('/api/login', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ username: u, password: p })
                });
                const data = await res.json();
                if (res.ok && data.success) {
                    currentUser = data.user;
                    onLoginSuccess(currentUser);
                } else {
                    if (errDiv) {
                        errDiv.textContent = data.error || 'Giriş başarısız!';
                        errDiv.style.display = 'block';
                    }
                }
            } catch (err) {
                if (errDiv) {
                    errDiv.textContent = 'Bağlantı hatası: ' + err.message;
                    errDiv.style.display = 'block';
                }
            } finally {
                if (submitBtn) {
                    submitBtn.disabled = false;
                    submitBtn.textContent = '🔑 Giriş Yap';
                }
            }
        }

        async function handleLogout() {
            if (confirm("Oturumu kapatmak istediğinize emin misiniz?")) {
                if (eventSource) {
                    try { eventSource.close(); } catch (e) {}
                    eventSource = null;
                }
                if (pollTimer) {
                    clearInterval(pollTimer);
                    pollTimer = null;
                }
                try { await fetch('/api/logout', { method: 'POST' }); } catch (e) {}
                currentUser = null;
                showLoginOverlay();
            }
        }

        // Navigation
        function switchPage(pageId) {
            currentPage = pageId;
            document.querySelectorAll('#sidebarNav .nav-item').forEach(el => {
                const target = el.getAttribute('data-page');
                el.classList.toggle('active', target === pageId);
            });
            document.querySelectorAll('.page-view').forEach(el => {
                el.classList.toggle('active', el.id === `page-${pageId}`);
            });

            const titles = {
                'dashboard': '📊 Genel Panel & Özet Metrikler',
                'monitor': '🔴 Canlı Hat Takibi & İstasyon Monitörü (SSE)',
                'servers': '🖥️ Aktif Yönetici Sunucuları & İstasyon Filosu',
                'users': '👥 Kullanıcı ve Yetki Yönetimi',
                'tests': '📋 Anten Test Kayıtları & Ölçüm Geçmişi',
                'orders': '📦 Üretim Siparişleri ve İş Emirleri',
                'profiles': '🛰️ Uydu Profilleri ve Tolerans Parametreleri',
                'database': '🗄️ Veritabanı Tarayıcısı & SQL Konsolu',
                'logs': '📜 Sistem Güvenlik ve Denetim İzi (Audit)'
            };
            document.getElementById('pageHeaderTitle').textContent = titles[pageId] || 'Yönetim Paneli';

            loadPageData(pageId);
        }

        function refreshCurrentPage() {
            loadPageData(currentPage);
        }

        function loadPageData(pageId) {
            if (pageId === 'dashboard') loadDashboard();
            else if (pageId === 'monitor') loadMonitorData();
            else if (pageId === 'servers') loadServers();
            else if (pageId === 'users') loadUsers();
            else if (pageId === 'tests') loadTests();
            else if (pageId === 'orders') loadOrders();
            else if (pageId === 'profiles') loadProfiles();
            else if (pageId === 'database') loadDatabase();
            else if (pageId === 'logs') loadAuditLogs();
        }

        // --- 0. Live Line Monitor & SSE Stream ---
        let eventSource = null;
        let pollTimer = null;

        function initLiveStream() {
            if (eventSource) {
                try { eventSource.close(); } catch (e) {}
                eventSource = null;
            }
            if (window.EventSource) {
                eventSource = new EventSource('/api/stream/events');
                eventSource.onopen = function() {
                    updateSseBadge(true, 'SSE');
                };
                eventSource.onmessage = function(e) {
                    try {
                        const data = JSON.parse(e.data);
                        renderLiveMonitor(data);
                    } catch (err) {
                        console.error('SSE parse error:', err);
                    }
                };
                eventSource.onerror = function() {
                    updateSseBadge(false);
                    if (!pollTimer) {
                        pollTimer = setInterval(pollLiveMonitor, 2500);
                    }
                };
            } else {
                if (!pollTimer) {
                    pollTimer = setInterval(pollLiveMonitor, 2500);
                }
            }
        }

        async function pollLiveMonitor() {
            try {
                const res = await fetch('/api/monitor/live');
                if (res.ok) {
                    const data = await res.json();
                    renderLiveMonitor(data);
                    updateSseBadge(true, 'POLL');
                }
            } catch (e) {
                updateSseBadge(false);
            }
        }

        function updateSseBadge(connected, mode) {
            const badge = document.getElementById('sseStatusBadge');
            if (!badge) return;
            if (connected) {
                badge.className = 'badge-pass';
                badge.textContent = mode === 'SSE' ? '🟢 SSE Canlı Yayın' : '🟡 Polling Aktif';
            } else {
                badge.className = 'badge-fail';
                badge.textContent = '🔴 Bağlantı Kesildi';
            }
        }

        async function loadMonitorData() {
            try {
                const res = await fetch('/api/monitor/live');
                if (res.ok) {
                    const data = await res.json();
                    renderLiveMonitor(data);
                }
            } catch (err) {
                console.error('loadMonitorData error:', err);
            }
        }

        function renderLiveMonitor(data) {
            if (!data) return;

            // 1. Time
            const timeEl = document.getElementById('sseTimeText');
            if (timeEl && data.timestamp) timeEl.textContent = data.timestamp;

            // 2. Lock Banner sync
            const lockTxt = document.getElementById('lockStatusText');
            if (lockTxt && data.lock) {
                if (data.lock.is_locked) {
                    lockTxt.innerHTML = `<span style="color:var(--warn-text);">🔒 Masa ${data.lock.locked_station} tarafından kilitli (Kanal: ${data.lock.channel_no})</span>`;
                    const lockTmEl = document.getElementById('lockTimeText');
                    if (lockTmEl) lockTmEl.textContent = `Kilit Zamanı: ${data.lock.acquired_at || 'Bilinmiyor'}`;
                } else {
                    lockTxt.innerHTML = `<span style="color:var(--primary-light);">🔓 Referans Almaç Müsait (Boşta)</span>`;
                    const lockTmEl = document.getElementById('lockTimeText');
                    if (lockTmEl) lockTmEl.textContent = 'Herhangi bir istasyon kilit talep edebilir.';
                }
            }

            // 3. Station Cards (Only show existing/active stations)
            const container = document.getElementById('monitorStationCards');
            if (container && data.stations) {
                if (data.stations.length === 0) {
                    container.innerHTML = `
                        <div class="card" style="grid-column: 1 / -1; padding: 26px; text-align: center; border-style: dashed;">
                            <div style="font-size: 15px; font-weight: bold; color: var(--text-main); margin-bottom: 6px;">📡 Şu Anda Aktif Test İstasyonu Bulunmuyor</div>
                            <div style="font-size: 12.5px; color: var(--text-muted);">Herhangi bir test istasyonu uygulaması açıldığında ve ağa bağlandığında otomatik olarak burada görünecektir.</div>
                        </div>
                    `;
                } else {
                    let html = '';
                    data.stations.forEach(st => {
                        let cardClass = 'station-card';
                        let badgeClass = 'station-badge-idle';
                        if (st.state === 'LOCKED') {
                            cardClass += ' locked';
                            badgeClass = 'station-badge-locked';
                        } else if (st.state === 'TESTING') {
                            cardClass += ' testing';
                            badgeClass = 'station-badge-testing';
                        } else if (st.state === 'OFFLINE') {
                            badgeClass = 'station-badge-offline';
                        }

                        let activeSection = '';
                        if (st.state === 'TESTING' && st.active_sn) {
                            activeSection = `
                                <div class="active-sn-banner">
                                    <div>
                                        <div style="font-size:10px; color:var(--accent-blue); font-weight:bold;">⚡ TEST EDİLEN ANTEN</div>
                                        <div style="font-size:13.5px; font-weight:bold; color:var(--text-main); font-family:monospace;">${st.active_sn}</div>
                                    </div>
                                    <span class="badge-pass" style="font-size:10px;">CANLI RF</span>
                                </div>
                            `;
                        } else if (st.state === 'LOCKED') {
                            activeSection = `
                                <div class="active-sn-banner" style="background-color:rgba(251,191,36,0.08); border-color:rgba(251,191,36,0.4);">
                                    <div>
                                        <div style="font-size:10px; color:var(--warn-text); font-weight:bold;">🔒 ALTIN ANTEN KİLİDİ</div>
                                        <div style="font-size:12px; color:var(--text-main);">Referans Almaç Meşgul</div>
                                    </div>
                                </div>
                            `;
                        }

                        let lastSection = '<div style="font-size:11px; color:var(--text-muted);">Henüz test kaydı yok</div>';
                        if (st.last_test) {
                            const resBadge = st.last_test.result === 'PASS' ? '<span class="badge-pass" style="font-size:10px;">PASS</span>' : '<span class="badge-fail" style="font-size:10px;">FAIL</span>';
                            lastSection = `
                                <div class="last-test-box">
                                    <div style="display:flex; justify-content:space-between; align-items:center;">
                                        <span style="font-family:monospace; font-weight:bold; color:var(--text-main);">${st.last_test.sn}</span>
                                        ${resBadge}
                                    </div>
                                    <div style="display:flex; justify-content:space-between; color:var(--text-muted); font-size:11px;">
                                        <span>Ort: <b style="color:var(--text-main);">${st.last_test.avg_delta} dB</b></span>
                                        <span>${st.last_test.duration} sn</span>
                                        <span>${st.last_test.timestamp ? st.last_test.timestamp.substring(11, 19) : ''}</span>
                                    </div>
                                </div>
                            `;
                        }

                        html += `
                            <div class="${cardClass}">
                                <div class="station-header">
                                    <div>
                                        <div class="station-title">
                                            <span>📡 ${st.station_name}</span>
                                        </div>
                                        <div class="station-ip">${st.ip}</div>
                                    </div>
                                    <span class="${badgeClass}">${st.state_label}</span>
                                </div>

                                ${activeSection}

                                <div class="stat-mini-grid">
                                    <div>
                                        <div class="stat-mini-val" style="color:var(--text-main);">${st.total_tests}</div>
                                        <div class="stat-mini-lbl">Toplam</div>
                                    </div>
                                    <div>
                                        <div class="stat-mini-val" style="color:var(--primary-light);">${st.pass_tests}</div>
                                        <div class="stat-mini-lbl">PASS (%${st.pass_rate})</div>
                                    </div>
                                    <div>
                                        <div class="stat-mini-val" style="color:var(--fail-text);">${st.fail_tests}</div>
                                        <div class="stat-mini-lbl">FAIL</div>
                                    </div>
                                </div>

                                <div>
                                    <div style="font-size:11px; color:var(--text-muted); margin-bottom:6px; font-weight:600;">Son Test:</div>
                                    ${lastSection}
                                </div>
                            </div>
                        `;
                    });
                    container.innerHTML = html;
                }
            }

            // 4. Recent Live Events Table
            const eventsBody = document.getElementById('monitorRecentEventsBody');
            if (eventsBody && data.recent_events) {
                if (data.recent_events.length === 0) {
                    eventsBody.innerHTML = '<tr><td colspan="8" style="color:var(--text-muted);">Henüz test kaydı bulunmuyor.</td></tr>';
                } else {
                    eventsBody.innerHTML = data.recent_events.map(ev => {
                        const badgeClass = ev.result === 'PASS' ? 'badge-pass' : 'badge-fail';
                        return `
                            <tr>
                                <td>${ev.id}</td>
                                <td style="font-weight:bold; font-family:monospace; color:var(--text-main);">${ev.sn}</td>
                                <td>Masa ${ev.station}</td>
                                <td>${ev.tester || '-'}</td>
                                <td><span class="${badgeClass}">${ev.result}</span></td>
                                <td><b>${ev.avg_delta}</b> dB</td>
                                <td>${ev.duration} sn</td>
                                <td style="color:var(--text-muted); font-size:11px;">${ev.timestamp}</td>
                            </tr>
                        `;
                    }).join('');
                }
            }
        }

        // 1. Dashboard
        async function loadDashboard() {
            try {
                const res = await fetch('/api/dashboard/stats');
                if (res.status === 401) { showLoginOverlay(); return; }
                const data = await res.json();

                document.getElementById('dashTotalTests').textContent = data.total_tests;
                document.getElementById('dashPassTests').textContent = data.pass_tests;
                document.getElementById('dashFailTests').textContent = data.fail_tests;
                document.getElementById('dashPassRate').textContent = `%${data.pass_rate} Başarı Oranı`;
                document.getElementById('dashActiveServers').textContent = data.active_servers;

                // Mutex Lock Banner
                const lockTxt = document.getElementById('lockStatusText');
                if (data.lock_station > 0) {
                    lockTxt.innerHTML = `<span style="color:var(--warn-text);">🔒 Masa ${data.lock_station} tarafından kilitli (Kanal: ${data.lock_channel})</span>`;
                    document.getElementById('lockTimeText').textContent = `Kilit Zamanı: ${data.lock_time || 'Bilinmiyor'}`;
                } else {
                    lockTxt.innerHTML = `<span style="color:var(--primary-light);">🔓 Referans Almaç Müsait (Boşta)</span>`;
                    document.getElementById('lockTimeText').textContent = 'Herhangi bir istasyon kilit talep edebilir.';
                }

                // Recent Tests
                const tbody = document.getElementById('dashRecentTestsBody');
                tbody.innerHTML = '';
                if (!data.recent_tests || data.recent_tests.length === 0) {
                    tbody.innerHTML = '<tr><td colspan="6" style="color:var(--text-muted);">Henüz test kaydı bulunmuyor.</td></tr>';
                    return;
                }
                data.recent_tests.forEach(t => {
                    const tr = document.createElement('tr');
                    const badgeClass = t.result === 'PASS' ? 'badge-pass' : 'badge-fail';
                    tr.innerHTML = `
                        <td><b>${t.sn}</b></td>
                        <td>Masa ${t.station}</td>
                        <td>${t.tester || '-'}</td>
                        <td>${t.avg_delta} dB</td>
                        <td><span class="badge ${badgeClass}">${t.result}</span></td>
                        <td style="color:var(--text-muted);">${t.timestamp}</td>
                    `;
                    tbody.appendChild(tr);
                });
            } catch (err) {
                console.error(err);
            }
        }

        // 2. Servers & Fleet
        async function loadServers() {
            try {
                const res = await fetch('/api/servers');
                if (res.status === 401) { showLoginOverlay(); return; }
                const data = await res.json();

                const hostBody = document.getElementById('serversHostTableBody');
                const stationBody = document.getElementById('serversStationTableBody');
                hostBody.innerHTML = '';
                stationBody.innerHTML = '';

                if (!data.hosts || data.hosts.length === 0) {
                    hostBody.innerHTML = '<tr><td colspan="6" style="color:var(--text-muted); text-align:center; padding:18px;">Şu anda ağda aktif yönetici sunucusu (Host PC) bulunmuyor.</td></tr>';
                } else {
                    data.hosts.forEach(h => {
                        const tr = document.createElement('tr');
                        const badge = h.status === 'ONLINE' ? 'badge-online' : 'badge-offline';
                        tr.innerHTML = `
                            <td><b>${h.name}</b></td>
                            <td><code>${h.ip}</code></td>
                            <td>${h.port}</td>
                            <td>${h.details || 'Yönetici Sunucu'}</td>
                            <td><span class="badge ${badge}">${h.status}</span></td>
                            <td style="color:var(--text-muted);">${h.last_seen}</td>
                        `;
                        hostBody.appendChild(tr);
                    });
                }

                if (!data.stations || data.stations.length === 0) {
                    stationBody.innerHTML = '<tr><td colspan="6" style="color:var(--text-muted); text-align:center; padding:18px;">Şu anda ağda bağlı test istasyonu bulunmuyor.</td></tr>';
                } else {
                    data.stations.forEach(s => {
                        const tr = document.createElement('tr');
                        const badge = s.status === 'ONLINE' || s.status === 'BUSY' ? 'badge-online' : 'badge-offline';
                        tr.innerHTML = `
                            <td><b>${s.name}</b></td>
                            <td><code>${s.ip}</code></td>
                            <td>${s.active_test_sn ? `<code>${s.active_test_sn}</code>` : '<span style="color:var(--text-muted);">-</span>'}</td>
                            <td>${s.details || 'Test Masası'}</td>
                            <td><span class="badge ${badge}">${s.status}</span></td>
                            <td style="color:var(--text-muted);">${s.last_seen}</td>
                        `;
                        stationBody.appendChild(tr);
                    });
                }
            } catch (err) {
                console.error(err);
            }
        }

        // 3. Users Management
        async function loadUsers() {
            try {
                const res = await fetch('/api/users');
                if (res.status === 401) { showLoginOverlay(); return; }
                const data = await res.json();
                const tbody = document.getElementById('usersTableBody');
                tbody.innerHTML = '';

                data.users.forEach(u => {
                    const tr = document.createElement('tr');
                    const roleBadge = u.role === 'ADMIN' ? 'badge-admin' : 'badge-role';
                    const activeBadge = u.is_active == 1 ? 'badge-online' : 'badge-offline';
                    const activeText = u.is_active == 1 ? 'Aktif' : 'Pasif';

                    const safeUser = (u.username || '').replace(/'/g, "\\'");
                    const safeFull = (u.full_name || '').replace(/'/g, "\\'");
                    tr.innerHTML = `
                        <td>#${u.id}</td>
                        <td><b>${u.username}</b></td>
                        <td>${u.full_name}</td>
                        <td><span class="badge ${roleBadge}">${u.role}</span></td>
                        <td><span class="badge ${activeBadge}">${activeText}</span></td>
                        <td style="color:var(--text-muted);">${u.created_at}</td>
                        <td style="display:flex; gap:6px; flex-wrap:wrap;">
                            <button class="btn btn-primary btn-sm" onclick="openEditUserModal(${u.id}, '${safeUser}', '${safeFull}', '${u.role}', ${u.is_active})">✏️ Düzenle</button>
                            ${u.username.toLowerCase() !== 'admin' ? `
                                <button class="btn btn-outline btn-sm" onclick="toggleUserRole(${u.id}, '${safeUser}', '${u.role}')" title="Yetki Değiştir (Admin/Operatör)">
                                    🔄 ${u.role === 'ADMIN' ? 'Operatör Yap' : 'Admin Yap'}
                                </button>
                            ` : `<span style="font-size:11px; color:var(--text-muted); align-self:center; padding:0 4px;">(Sistem Yöneticisi)</span>`}
                            <button class="btn btn-outline btn-sm" onclick="openUpdatePasswordModal(${u.id}, '${safeUser}')">🔑 Şifre</button>
                            ${u.username.toLowerCase() !== 'admin' ? `
                                <button class="btn btn-outline btn-sm" onclick="toggleUserActive(${u.id}, ${u.is_active})">${u.is_active == 1 ? 'Pasif Yap' : 'Aktif Yap'}</button>
                                <button class="btn btn-danger btn-sm" onclick="deleteUser(${u.id}, '${safeUser}')">Sil</button>
                            ` : ''}
                        </td>
                    `;
                    tbody.appendChild(tr);
                });
            } catch (err) {
                console.error(err);
            }
        }

        function openCreateUserModal() {
            document.getElementById('newUsername').value = '';
            document.getElementById('newFullName').value = '';
            document.getElementById('newPassword').value = '';
            document.getElementById('modalCreateUser').classList.add('active');
        }

        function openEditUserModal(id, username, fullName, role, isActive) {
            const isAdminAccount = (username || '').toLowerCase() === 'admin';
            document.getElementById('editUserId').value = id;
            const userInp = document.getElementById('editUsername');
            userInp.value = username || '';
            userInp.disabled = isAdminAccount;

            document.getElementById('editFullName').value = fullName || '';

            const roleSel = document.getElementById('editRole');
            roleSel.value = role === 'ADMIN' ? 'ADMIN' : 'OPERATOR';
            roleSel.disabled = isAdminAccount;

            const actSel = document.getElementById('editIsActive');
            actSel.value = isActive == 1 ? '1' : '0';
            actSel.disabled = isAdminAccount;

            document.getElementById('editPassword').value = '';
            document.getElementById('modalEditUser').classList.add('active');
        }

        async function handleEditUser(e) {
            e.preventDefault();
            const id = document.getElementById('editUserId').value;
            const u = document.getElementById('editUsername').value.trim();
            const fn = document.getElementById('editFullName').value.trim();
            const r = document.getElementById('editRole').value;
            const act = parseInt(document.getElementById('editIsActive').value, 10);
            const p = document.getElementById('editPassword').value.trim();

            try {
                const res = await fetch('/api/users/update', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ id: id, username: u, full_name: fn, role: r, is_active: act, password: p })
                });
                const data = await res.json();
                if (res.ok && data.success) {
                    closeModal('modalEditUser');
                    loadUsers();
                } else {
                    alert('Hata: ' + (data.error || 'Kullanıcı güncellenemedi'));
                }
            } catch (err) {
                alert('Bağlantı hatası: ' + err.message);
            }
        }

        async function handleCreateUser(e) {
            e.preventDefault();
            const u = document.getElementById('newUsername').value.trim();
            const fn = document.getElementById('newFullName').value.trim();
            const p = document.getElementById('newPassword').value.trim();
            const r = document.getElementById('newRole').value;

            try {
                const res = await fetch('/api/users/create', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ username: u, full_name: fn, password: p, role: r })
                });
                const data = await res.json();
                if (res.ok && data.success) {
                    closeModal('modalCreateUser');
                    loadUsers();
                } else {
                    alert('Hata: ' + (data.error || 'Kullanıcı oluşturulamadı'));
                }
            } catch (err) {
                alert('Bağlantı hatası: ' + err.message);
            }
        }

        function openUpdatePasswordModal(userId, username) {
            document.getElementById('passUserId').value = userId;
            document.getElementById('passUsername').value = username;
            document.getElementById('passNewPassword').value = '';
            document.getElementById('modalUpdatePassword').classList.add('active');
        }

        async function handleUpdatePassword(e) {
            e.preventDefault();
            const id = document.getElementById('passUserId').value;
            const p = document.getElementById('passNewPassword').value.trim();

            try {
                const res = await fetch('/api/users/update-password', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ id: id, password: p })
                });
                const data = await res.json();
                if (res.ok && data.success) {
                    closeModal('modalUpdatePassword');
                    alert('Şifre başarıyla güncellendi!');
                } else {
                    alert('Hata: ' + data.error);
                }
            } catch (err) {
                alert('Hata: ' + err.message);
            }
        }

        async function toggleUserRole(userId, username, currentRole) {
            const nextRole = currentRole === 'ADMIN' ? 'OPERATOR' : 'ADMIN';
            const nextLabel = nextRole === 'ADMIN' ? 'YÖNETİCİ (ADMIN)' : 'OPERATÖR';
            if (!confirm(`'${username}' kullanıcısının yetkisini '${nextLabel}' olarak güncellemek istediğinize emin misiniz?`)) {
                return;
            }
            try {
                const res = await fetch('/api/users/change-role', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ id: userId, role: nextRole })
                });
                const data = await res.json();
                if (res.ok && data.success) {
                    loadUsers();
                } else {
                    alert('Hata: ' + (data.error || 'Yetki güncellenemedi'));
                }
            } catch (err) {
                alert('Bağlantı hatası: ' + err.message);
            }
        }

        async function toggleUserActive(userId, currentActive) {
            const nextActive = currentActive == 1 ? 0 : 1;
            try {
                const res = await fetch('/api/users/toggle', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ id: userId, is_active: nextActive })
                });
                if (res.ok) loadUsers();
            } catch (err) {
                alert('Hata: ' + err.message);
            }
        }

        async function deleteUser(userId, username) {
            if (confirm(`'${username}' kullanıcısını silmek istediğinize emin misiniz?`)) {
                try {
                    const res = await fetch('/api/users/delete', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ id: userId })
                    });
                    if (res.ok) loadUsers();
                } catch (err) {
                    alert('Hata: ' + err.message);
                }
            }
        }

        // 4. Tests Page
        async function loadTests() {
            try {
                const res = await fetch('/api/tests');
                if (res.status === 401) { showLoginOverlay(); return; }
                const data = await res.json();
                allTestsCache = data.tests;
                renderTests(allTestsCache);
            } catch (err) {
                console.error(err);
            }
        }

        function renderTests(tests) {
            const tbody = document.getElementById('testsTableBody');
            tbody.innerHTML = '';
            if (!tests || tests.length === 0) {
                tbody.innerHTML = '<tr><td colspan="9" style="color:var(--text-muted);">Kayıt bulunamadı.</td></tr>';
                return;
            }

            tests.forEach(t => {
                const tr = document.createElement('tr');
                const badgeClass = t.result === 'PASS' ? 'badge-pass' : 'badge-fail';
                tr.innerHTML = `
                    <td>#${t.id}</td>
                    <td><b>${t.sn}</b></td>
                    <td>Masa ${t.station}</td>
                    <td>${t.tester || '-'}</td>
                    <td><span class="badge ${badgeClass}">${t.result}</span></td>
                    <td>${t.avg_delta} dB</td>
                    <td>${t.duration} sn</td>
                    <td style="color:var(--text-muted);">${t.timestamp}</td>
                    <td><button class="btn btn-outline btn-sm" onclick="openTestDetail(${t.id}, '${t.sn}')">🔍 Detay</button></td>
                `;
                tbody.appendChild(tr);
            });
        }

        function filterTests() {
            const query = document.getElementById('testSearchInput').value.toLowerCase();
            const resFilter = document.getElementById('testResultFilter').value;

            const filtered = allTestsCache.filter(t => {
                const matchQ = !query || String(t.sn).toLowerCase().includes(query) || String(t.tester).toLowerCase().includes(query);
                const matchR = !resFilter || t.result === resFilter;
                return matchQ && matchR;
            });
            renderTests(filtered);
        }

        async function openTestDetail(runId, sn) {
            document.getElementById('testDetailTitle').textContent = `🔍 Ölçüm Detayı: Anten ${sn} (#ID: ${runId})`;
            const tbody = document.getElementById('testDetailChannelsBody');
            tbody.innerHTML = '<tr><td colspan="8">Yükleniyor...</td></tr>';
            document.getElementById('modalTestDetail').classList.add('active');

            try {
                const res = await fetch(`/api/tests/detail?id=${runId}`);
                const data = await res.json();
                tbody.innerHTML = '';
                if (!data.channels || data.channels.length === 0) {
                    tbody.innerHTML = '<tr><td colspan="8" style="color:var(--text-muted);">Bu test için kanal detay verisi bulunamadı.</td></tr>';
                    return;
                }
                data.channels.forEach(ch => {
                    const tr = document.createElement('tr');
                    const badgeClass = ch.result === 'PASS' ? 'badge-pass' : 'badge-fail';
                    tr.innerHTML = `
                        <td>${ch.no}</td>
                        <td><b>${ch.name}</b></td>
                        <td>${ch.freq} MHz (${ch.pol})</td>
                        <td>${ch.dut_cn} dB</td>
                        <td>${ch.ref_cn} dB</td>
                        <td>${ch.delta} dB</td>
                        <td><code>S:${ch.s ? '+' : '-'} V:${ch.v ? '+' : '-'} A:${ch.a ? '+' : '-'}</code></td>
                        <td><span class="badge ${badgeClass}">${ch.result}</span></td>
                    `;
                    tbody.appendChild(tr);
                });
            } catch (err) {
                tbody.innerHTML = `<tr><td colspan="8" style="color:var(--fail-text);">Hata: ${err.message}</td></tr>`;
            }
        }

        // 5. Work Orders
        async function loadOrders() {
            try {
                const res = await fetch('/api/work-orders');
                if (res.status === 401) { showLoginOverlay(); return; }
                const data = await res.json();
                const tbody = document.getElementById('ordersTableBody');
                tbody.innerHTML = '';
                if (!data.orders || data.orders.length === 0) {
                    tbody.innerHTML = '<tr><td colspan="9" style="color:var(--text-muted);">Kayıtlı iş emri bulunmuyor.</td></tr>';
                    return;
                }
                data.orders.forEach(o => {
                    const tr = document.createElement('tr');
                    tr.innerHTML = `
                        <td><b>${o.order_no}</b></td>
                        <td>${o.customer}</td>
                        <td><code>${o.product_code}</code></td>
                        <td><b>${o.target_quantity}</b></td>
                        <td>${o.completed_quantity}</td>
                        <td style="color:var(--pass-text);">${o.passed_quantity}</td>
                        <td style="color:var(--fail-text);">${o.failed_quantity}</td>
                        <td><span class="badge badge-role">${o.status}</span></td>
                        <td style="color:var(--text-muted);">${o.created_at}</td>
                    `;
                    tbody.appendChild(tr);
                });
            } catch (err) {
                console.error(err);
            }
        }

        function openCreateOrderModal() {
            document.getElementById('newOrderNo').value = `NETA-${new Date().getFullYear()}-00${Math.floor(Math.random()*90)+10}`;
            document.getElementById('newOrderCustomer').value = '';
            document.getElementById('newOrderProduct').value = 'DRX9000-ANT-V1';
            document.getElementById('modalCreateOrder').classList.add('active');
        }

        async function handleCreateOrder(e) {
            e.preventDefault();
            const no = document.getElementById('newOrderNo').value.trim();
            const cust = document.getElementById('newOrderCustomer').value.trim();
            const prod = document.getElementById('newOrderProduct').value.trim();
            const qty = parseInt(document.getElementById('newOrderQuantity').value, 10);

            try {
                const res = await fetch('/api/work-orders/create', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ order_no: no, customer: cust, product_code: prod, target: qty })
                });
                const data = await res.json();
                if (res.ok && data.success) {
                    closeModal('modalCreateOrder');
                    loadOrders();
                } else {
                    alert('Hata: ' + data.error);
                }
            } catch (err) {
                alert('Hata: ' + err.message);
            }
        }

        // 6. Profiles Page
        let currentSelectedProfileId = 1;
        let cachedProfiles = [];

        async function loadProfiles(targetProfileId) {
            if (targetProfileId) currentSelectedProfileId = targetProfileId;
            try {
                const res = await fetch(`/api/profiles?profile_id=${currentSelectedProfileId}`);
                if (res.status === 401) { showLoginOverlay(); return; }
                const data = await res.json();
                cachedProfiles = data.profiles || [];
                currentSelectedProfileId = data.selected_profile_id || currentSelectedProfileId;

                // Render Profiles Table & Dropdown
                const pBody = document.getElementById('profilesTableBody');
                pBody.innerHTML = '';
                const pSelect = document.getElementById('profileSelectDropdown');
                pSelect.innerHTML = '';

                data.profiles.forEach(p => {
                    const opt = document.createElement('option');
                    opt.value = p.id;
                    opt.textContent = `#${p.id} - ${p.name} (${p.satellite})`;
                    if (p.id === currentSelectedProfileId) opt.selected = true;
                    pSelect.appendChild(opt);

                    const tr = document.createElement('tr');
                    if (p.id === currentSelectedProfileId) {
                        tr.style.backgroundColor = 'rgba(0, 210, 106, 0.08)';
                    }
                    tr.innerHTML = `
                        <td>#${p.id}</td>
                        <td><b>${p.name}</b></td>
                        <td>${p.satellite}</td>
                        <td>${p.orbital}</td>
                        <td>± ${p.tolerance} dB</td>
                        <td><span class="badge badge-online">Aktif</span></td>
                        <td style="display:flex; gap:6px;">
                            <button class="btn btn-outline btn-sm" onclick="selectProfile(${p.id})">🔍 Frekansları Gör</button>
                            ${p.id > 1 ? `<button class="btn btn-danger btn-sm" onclick="deleteProfile(${p.id}, '${p.name}')">Sil</button>` : `<span style="font-size:11px; color:var(--text-muted); align-self:center;">(Varsayılan)</span>`}
                        </td>
                    `;
                    pBody.appendChild(tr);
                });

                const curProf = data.profiles.find(p => p.id === currentSelectedProfileId);
                const titleStr = curProf ? `📡 ${curProf.name} (${curProf.satellite} - ${curProf.orbital}) Kanal & Frekans Listesi` : '📡 Kanal & Frekans Listesi';
                document.getElementById('selectedProfileTitle').textContent = titleStr;

                // Render Channels Table
                const cBody = document.getElementById('profileChannelsTableBody');
                cBody.innerHTML = '';
                if (!data.channels || data.channels.length === 0) {
                    cBody.innerHTML = '<tr><td colspan="7" style="color:var(--text-muted); text-align:center; padding:16px;">Bu profile henüz kanal/frekans tanımlanmamış. "Yeni Frekans / Kanal Ekle" butonu ile ekleyebilirsiniz.</td></tr>';
                } else {
                    data.channels.forEach(c => {
                        const tr = document.createElement('tr');
                        tr.innerHTML = `
                            <td><b>#${c.no}</b></td>
                            <td><b>${c.name}</b></td>
                            <td><span style="color:var(--primary-light); font-weight:bold;">${c.freq}</span> MHz</td>
                            <td>${c.sr} kS/s</td>
                            <td><code>${c.pol}</code></td>
                            <td>± ${c.tol} dB</td>
                            <td>
                                <button class="btn btn-danger btn-sm" onclick="deleteChannel(${currentSelectedProfileId}, ${c.no}, '${c.name}')">Sil</button>
                            </td>
                        `;
                        cBody.appendChild(tr);
                    });
                }
            } catch (err) {
                console.error(err);
            }
        }

        function onProfileDropdownChange() {
            const sel = document.getElementById('profileSelectDropdown');
            const pid = parseInt(sel.value, 10);
            if (pid > 0) {
                loadProfiles(pid);
            }
        }

        function selectProfile(profileId) {
            loadProfiles(profileId);
        }

        function openCreateProfileModal() {
            document.getElementById('newProfName').value = '';
            document.getElementById('newProfSatellite').value = '';
            document.getElementById('newProfOrbital').value = '42.0E';
            document.getElementById('newProfTolerance').value = '1.50';
            document.getElementById('modalCreateProfile').classList.add('active');
        }

        async function handleCreateProfile(e) {
            e.preventDefault();
            const name = document.getElementById('newProfName').value.trim();
            const sat = document.getElementById('newProfSatellite').value.trim();
            const orb = document.getElementById('newProfOrbital').value.trim();
            const tol = parseFloat(document.getElementById('newProfTolerance').value) || 1.50;

            try {
                const res = await fetch('/api/profiles/create', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ name: name, satellite: sat, orbital: orb, tolerance: tol })
                });
                const data = await res.json();
                if (res.ok && data.success) {
                    closeModal('modalCreateProfile');
                    loadProfiles();
                } else {
                    alert('Hata: ' + (data.error || 'Profil oluşturulamadı'));
                }
            } catch (err) {
                alert('Bağlantı hatası: ' + err.message);
            }
        }

        async function deleteProfile(profileId, profileName) {
            if (confirm(`'${profileName}' uydu profilini ve bu profile ait tüm kanalları silmek istediğinize emin misiniz?`)) {
                try {
                    const res = await fetch('/api/profiles/delete', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ id: profileId })
                    });
                    const data = await res.json();
                    if (res.ok && data.success) {
                        currentSelectedProfileId = 1;
                        loadProfiles();
                    } else {
                        alert('Hata: ' + (data.error || 'Profil silinemedi'));
                    }
                } catch (err) {
                    alert('Bağlantı hatası: ' + err.message);
                }
            }
        }

        function openAddChannelModal() {
            const curProf = cachedProfiles.find(p => p.id === currentSelectedProfileId);
            const profName = curProf ? `#${curProf.id} - ${curProf.name} (${curProf.satellite})` : `#${currentSelectedProfileId}`;
            document.getElementById('channelTargetProfileName').value = profName;
            document.getElementById('chNum').value = '';
            document.getElementById('chName').value = '';
            document.getElementById('chFreq').value = '';
            document.getElementById('chSr').value = '30000';
            document.getElementById('chPol').value = 'V';
            document.getElementById('chTol').value = curProf ? curProf.tolerance : '1.50';
            document.getElementById('modalAddChannel').classList.add('active');
        }

        async function handleAddChannel(e) {
            e.preventDefault();
            const chNo = parseInt(document.getElementById('chNum').value, 10);
            const chName = document.getElementById('chName').value.trim();
            const freq = parseInt(document.getElementById('chFreq').value, 10);
            const sr = parseInt(document.getElementById('chSr').value, 10);
            const pol = document.getElementById('chPol').value;
            const tol = parseFloat(document.getElementById('chTol').value) || 1.50;

            try {
                const res = await fetch('/api/profiles/channels/create', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        profile_id: currentSelectedProfileId,
                        channel_no: chNo,
                        channel_name: chName,
                        frequency_mhz: freq,
                        symbol_rate: sr,
                        polarization: pol,
                        tolerance_db: tol
                    })
                });
                const data = await res.json();
                if (res.ok && data.success) {
                    closeModal('modalAddChannel');
                    loadProfiles(currentSelectedProfileId);
                } else {
                    alert('Hata: ' + (data.error || 'Kanal kaydedilemedi'));
                }
            } catch (err) {
                alert('Bağlantı hatası: ' + err.message);
            }
        }

        async function deleteChannel(profileId, channelNo, channelName) {
            if (confirm(`'#${channelNo} - ${channelName}' kanalını silmek istediğinize emin misiniz?`)) {
                try {
                    const res = await fetch('/api/profiles/channels/delete', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ profile_id: profileId, channel_no: channelNo })
                    });
                    const data = await res.json();
                    if (res.ok && data.success) {
                        loadProfiles(profileId);
                    } else {
                        alert('Hata: ' + (data.error || 'Kanal silinemedi'));
                    }
                } catch (err) {
                    alert('Bağlantı hatası: ' + err.message);
                }
            }
        }

        // 7. Database Page (Raw)
        async function loadDatabase() {
            try {
                const res = await fetch('/api/tables');
                if (res.status === 401) { showLoginOverlay(); return; }
                const data = await res.json();
                const list = document.getElementById('dbTableList');
                list.innerHTML = '';
                data.tables.forEach(t => {
                    const li = document.createElement('li');
                    li.className = `nav-item ${t.name === currentRawTable ? 'active' : ''}`;
                    li.innerHTML = `<span>${t.name}</span><span style="margin-left:auto; font-size:10.5px; opacity:0.7;">(${t.rows})</span>`;
                    li.onclick = () => loadRawTable(t.name);
                    list.appendChild(li);
                });
                loadRawTable(currentRawTable);
            } catch (err) {
                console.error(err);
            }
        }

        async function loadRawTable(tableName) {
            currentRawTable = tableName;
            document.querySelectorAll('#dbTableList .nav-item').forEach(el => {
                el.classList.toggle('active', el.querySelector('span').textContent === tableName);
            });
            document.getElementById('rawTableNameTitle').textContent = `Tablo: ${tableName}`;

            try {
                const res = await fetch(`/api/data?table=${encodeURIComponent(tableName)}`);
                const data = await res.json();
                const headTr = document.getElementById('rawTableHead');
                const tbody = document.getElementById('rawTableBody');
                headTr.innerHTML = '';
                tbody.innerHTML = '';

                data.headers.forEach(h => {
                    const th = document.createElement('th');
                    th.textContent = h;
                    headTr.appendChild(th);
                });

                document.getElementById('rawRecordCount').textContent = `${data.rows.length} Kayıt`;

                data.rows.forEach(r => {
                    const tr = document.createElement('tr');
                    r.forEach(val => {
                        const td = document.createElement('td');
                        td.textContent = val !== null ? val : 'NULL';
                        tr.appendChild(td);
                    });
                    tbody.appendChild(tr);
                });
            } catch (err) {
                console.error(err);
            }
        }

        function toggleDbSubTab(tab) {
            document.getElementById('btnDbViewTable').classList.toggle('btn-primary', tab === 'table');
            document.getElementById('btnDbViewTable').classList.toggle('btn-outline', tab !== 'table');
            document.getElementById('btnDbViewSql').classList.toggle('btn-primary', tab === 'sql');
            document.getElementById('btnDbViewSql').classList.toggle('btn-outline', tab !== 'sql');
            document.getElementById('rawTableView').style.display = tab === 'table' ? 'flex' : 'none';
            document.getElementById('rawSqlView').style.display = tab === 'sql' ? 'flex' : 'none';
        }

        async function runCustomSql() {
            const q = document.getElementById('sqlInput').value.trim();
            if (!q) return;
            const statusDiv = document.getElementById('sqlStatus');
            statusDiv.innerHTML = '<div style="font-size:12px; color:var(--text-muted);">Sorgu çalıştırılıyor...</div>';

            try {
                const res = await fetch('/api/query', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ query: q })
                });
                const data = await res.json();
                if (data.error) {
                    statusDiv.innerHTML = `<div style="color:var(--fail-text); font-size:12px;">❌ ${data.error}</div>`;
                    return;
                }
                statusDiv.innerHTML = `<div style="color:var(--pass-text); font-size:12px;">✔ Sorgu tamamlandı (${data.rows.length} Satır)</div>`;
                const headTr = document.getElementById('sqlHead');
                const tbody = document.getElementById('sqlBody');
                headTr.innerHTML = '';
                tbody.innerHTML = '';

                data.headers.forEach(h => {
                    const th = document.createElement('th');
                    th.textContent = h;
                    headTr.appendChild(th);
                });

                data.rows.forEach(r => {
                    const tr = document.createElement('tr');
                    r.forEach(val => {
                        const td = document.createElement('td');
                        td.textContent = val !== null ? val : 'NULL';
                        tr.appendChild(td);
                    });
                    tbody.appendChild(tr);
                });
            } catch (err) {
                statusDiv.innerHTML = `<div style="color:var(--fail-text); font-size:12px;">❌ Hata: ${err.message}</div>`;
            }
        }

        // 8. Audit Logs Page
        async function loadAuditLogs() {
            try {
                const res = await fetch('/api/logs');
                if (res.status === 401) { showLoginOverlay(); return; }
                const data = await res.json();
                const tbody = document.getElementById('auditLogsTableBody');
                tbody.innerHTML = '';
                if (!data.logs || data.logs.length === 0) {
                    tbody.innerHTML = '<tr><td colspan="5" style="color:var(--text-muted);">Kayıtlı sistem logu bulunamadı.</td></tr>';
                    return;
                }
                data.logs.forEach(l => {
                    const tr = document.createElement('tr');
                    const badge = l.level === 'ERROR' ? 'badge-fail' : (l.level === 'WARNING' ? 'badge-fail' : 'badge-pass');
                    tr.innerHTML = `
                        <td>#${l.id}</td>
                        <td><span class="badge ${badge}">${l.level}</span></td>
                        <td><code>${l.source}</code></td>
                        <td>${l.message}</td>
                        <td style="color:var(--text-muted);">${l.created_at}</td>
                    `;
                    tbody.appendChild(tr);
                });
            } catch (err) {
                console.error(err);
            }
        }

        function closeModal(modalId) {
            document.getElementById(modalId).classList.remove('active');
        }

        function exportCsv(tbl) {
            const tableToExport = tbl || currentRawTable || 'test_runs';
            window.location.href = `/api/export?table=${encodeURIComponent(tableToExport)}&format=csv`;
        }

        function exportExcel(tbl) {
            const tableToExport = tbl || currentRawTable || 'test_runs';
            window.location.href = `/api/export?table=${encodeURIComponent(tableToExport)}&format=excel`;
        }

        // Boot & Event Listeners
        window.addEventListener('DOMContentLoaded', () => {
            const pInput = document.getElementById('loginPassword');
            if (pInput) pInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') handleLogin(e); });
            const uInput = document.getElementById('loginUsername');
            if (uInput) uInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') handleLogin(e); });
        });
        checkAuth();
    </script>
</body>
</html>
"""

class NetaWebHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        qs = urllib.parse.parse_qs(parsed.query)

        # 1. Main SPA Page & Static
        if path == "/" or path == "/index.html":
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
            self.send_header("Pragma", "no-cache")
            self.send_header("Expires", "0")
            self.send_header("Content-Security-Policy", "default-src * 'unsafe-inline' 'unsafe-eval' data: blob:; script-src * 'unsafe-inline' 'unsafe-eval'; style-src * 'unsafe-inline';")
            self.end_headers()
            self.wfile.write(HTML_PAGE.encode("utf-8"))
            return

        elif path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
            return

        # 2. Check Auth Status (/api/me)
        elif path == "/api/me":
            user = get_session_user(self)
            if user:
                self.send_json({"authenticated": True, "user": user})
            else:
                self.send_json({"authenticated": False, "user": None}, 200)
            return

        # 3. Protected Endpoints (Require valid session)
        user = get_session_user(self)
        if not user:
            self.send_json({"error": "Oturum açmanız gerekiyor."}, 401)
            return

        # Server-Sent Events (SSE) Live Stream
        if path == "/api/stream/events":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream; charset=utf-8")
            self.send_header("Cache-Control", "no-cache, no-transform")
            self.send_header("Connection", "keep-alive")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()

            while True:
                try:
                    payload = get_live_monitor_payload()
                    msg = f"data: {json.dumps(payload, ensure_ascii=False)}\n\n"
                    self.wfile.write(msg.encode("utf-8"))
                    self.wfile.flush()
                    time.sleep(1.0)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    break
                except Exception:
                    time.sleep(1.0)
            return

        # Live Line Monitor Snapshot (JSON Fallback)
        elif path == "/api/monitor/live":
            self.send_json(get_live_monitor_payload())
            return

        # Dashboard Stats
        elif path == "/api/dashboard/stats":
            _, r_tot, _ = run_sql("SELECT COUNT(*) FROM test_runs;")
            tot = int(r_tot[0][0]) if r_tot and r_tot[0][0] else 0

            _, r_pass, _ = run_sql("SELECT COUNT(*) FROM test_runs WHERE overall_result = 'PASS';")
            passed = int(r_pass[0][0]) if r_pass and r_pass[0][0] else 0

            _, r_fail, _ = run_sql("SELECT COUNT(*) FROM test_runs WHERE overall_result = 'FAIL';")
            failed = int(r_fail[0][0]) if r_fail and r_fail[0][0] else 0

            pass_rate = round((passed / tot * 100), 1) if tot > 0 else 0.0

            _, r_srv, _ = run_sql("SELECT COUNT(*) FROM cluster_nodes WHERE node_type = 'HOST_SERVER' AND status != 'OFFLINE' AND TIMESTAMPDIFF(SECOND, last_seen, NOW()) <= 90;")
            active_servers = safe_int(r_srv[0][0]) if r_srv and len(r_srv[0]) > 0 else 0

            _, r_lock, _ = run_sql("SELECT locked_by_station, current_channel_no, lock_acquired_at FROM reference_lock WHERE id = 1;")
            lock_st = int(r_lock[0][0]) if r_lock and r_lock[0][0] else 0
            lock_ch = int(r_lock[0][1]) if r_lock and r_lock[0][1] else 0
            lock_tm = str(r_lock[0][2]) if r_lock and len(r_lock[0]) > 2 else ""

            _, r_rec, _ = run_sql("SELECT antenna_sn, station_id, tester_name, avg_delta_db, overall_result, test_timestamp FROM test_runs ORDER BY id DESC LIMIT 5;")
            recent = []
            for r in r_rec:
                recent.append({
                    "sn": r[0], "station": r[1], "tester": r[2], "avg_delta": r[3], "result": r[4], "timestamp": r[5]
                })

            self.send_json({
                "total_tests": tot, "pass_tests": passed, "fail_tests": failed,
                "pass_rate": pass_rate, "active_servers": active_servers,
                "lock_station": lock_st, "lock_channel": lock_ch, "lock_time": lock_tm,
                "recent_tests": recent
            })
            return

        # Servers & Fleet (Only show active, non-expired Host servers and Stations)
        elif path == "/api/servers":
            _, r_hosts, _ = run_sql(
                "SELECT node_name, ip_address, port, details, status, last_seen "
                "FROM cluster_nodes "
                "WHERE node_type = 'HOST_SERVER' AND status != 'OFFLINE' "
                "AND TIMESTAMPDIFF(SECOND, last_seen, NOW()) <= 90 "
                "ORDER BY id ASC;"
            )
            hosts = [{"name": r[0], "ip": r[1], "port": r[2], "details": r[3], "status": r[4], "last_seen": r[5]} for r in r_hosts]

            _, r_st, _ = run_sql(
                "SELECT node_name, ip_address, active_test_sn, details, status, last_seen "
                "FROM cluster_nodes "
                "WHERE node_type = 'STATION_CLIENT' AND status != 'OFFLINE' "
                "AND TIMESTAMPDIFF(SECOND, last_seen, NOW()) <= 90 "
                "ORDER BY port ASC, id ASC;"
            )
            stations = [{"name": r[0], "ip": r[1], "active_test_sn": r[2], "details": r[3], "status": r[4], "last_seen": r[5]} for r in r_st]

            self.send_json({"hosts": hosts, "stations": stations})
            return

        # Users List
        elif path == "/api/users":
            _, rows, _ = run_sql("SELECT id, username, full_name, role, is_active, created_at FROM users ORDER BY id ASC;")
            users = [{"id": r[0], "username": r[1], "full_name": r[2], "role": r[3], "is_active": r[4], "created_at": r[5]} for r in rows]
            self.send_json({"users": users})
            return

        # Test Runs List
        elif path == "/api/tests":
            _, rows, _ = run_sql("SELECT id, antenna_sn, station_id, tester_name, overall_result, avg_delta_db, duration_sec, test_timestamp FROM test_runs ORDER BY id DESC LIMIT 100;")
            tests = [{
                "id": r[0], "sn": r[1], "station": r[2], "tester": r[3], "result": r[4],
                "avg_delta": r[5], "duration": r[6], "timestamp": r[7]
            } for r in rows]
            self.send_json({"tests": tests})
            return

        # Test Detail (10 Channels)
        elif path == "/api/tests/detail":
            run_id = qs.get("id", ["0"])[0]
            safe_id = "".join(c for c in run_id if c.isdigit())
            _, rows, _ = run_sql(
                f"SELECT channel_no, channel_name, frequency_mhz, polarization, dut_cn_avg, ref_cn_avg, delta_db, carrier_locked, video_locked, audio_locked, result "
                f"FROM test_run_channels WHERE test_run_id = {safe_id} ORDER BY channel_no ASC;"
            )
            channels = [{
                "no": r[0], "name": r[1], "freq": r[2], "pol": r[3], "dut_cn": r[4],
                "ref_cn": r[5], "delta": r[6], "s": r[7] == '1', "v": r[8] == '1', "a": r[9] == '1', "result": r[10]
            } for r in rows]
            self.send_json({"channels": channels})
            return

        # Work Orders
        elif path == "/api/work-orders":
            _, rows, _ = run_sql("SELECT order_no, customer_name, product_code, target_quantity, completed_quantity, passed_quantity, failed_quantity, status, created_at FROM work_orders ORDER BY id DESC;")
            orders = [{
                "order_no": r[0], "customer": r[1], "product_code": r[2], "target_quantity": r[3],
                "completed_quantity": r[4], "passed_quantity": r[5], "failed_quantity": r[6],
                "status": r[7], "created_at": r[8]
            } for r in rows]
            self.send_json({"orders": orders})
            return

        # Profiles
        elif path == "/api/profiles":
            _, r_p, _ = run_sql("SELECT id, profile_name, satellite_name, orbital_position, default_tolerance_db, is_active FROM satellite_profiles ORDER BY id ASC;")
            profiles = [{"id": r[0], "name": r[1], "satellite": r[2], "orbital": r[3], "tolerance": r[4], "is_active": r[5]} for r in r_p]

            try:
                profile_id = int(qs.get("profile_id", [1])[0])
            except (ValueError, IndexError):
                profile_id = 1
            if not any(p["id"] == profile_id for p in profiles) and profiles:
                profile_id = profiles[0]["id"]

            _, r_c, _ = run_sql(f"SELECT channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db FROM profile_channels WHERE profile_id = {profile_id} ORDER BY channel_no ASC;")
            channels = [{"no": r[0], "name": r[1], "freq": r[2], "sr": r[3], "pol": r[4], "tol": r[5]} for r in r_c]

            self.send_json({"profiles": profiles, "channels": channels, "selected_profile_id": profile_id})
            return

        # Audit Logs
        elif path == "/api/logs":
            _, rows, _ = run_sql("SELECT id, log_level, source, message, created_at FROM system_logs ORDER BY id DESC LIMIT 100;")
            logs = [{"id": r[0], "level": r[1], "source": r[2], "message": r[3], "created_at": r[4]} for r in rows]
            self.send_json({"logs": logs})
            return

        # Raw Tables & Data
        elif path == "/api/tables":
            headers, rows, err = run_sql("SHOW TABLE STATUS;")
            if err:
                self.send_json({"error": err}, 500)
                return
            tables = []
            for r in rows:
                if len(r) >= 5:
                    tables.append({"name": r[0], "rows": r[4] or 0})
            self.send_json({"tables": tables})
            return

        elif path == "/api/data":
            tbl = qs.get("table", ["test_runs"])[0]
            safe_tbl = "".join(c for c in tbl if c.isalnum() or c == '_')
            headers, rows, err = run_sql(f"SELECT * FROM `{safe_tbl}` ORDER BY 1 DESC LIMIT 300;")
            if err:
                self.send_json({"error": err}, 500)
                return
            self.send_json({"headers": headers, "rows": rows})
            return

        elif path == "/api/export":
            tbl = qs.get("table", ["test_runs"])[0]
            fmt = qs.get("format", ["csv"])[0].lower()
            safe_tbl = "".join(c for c in tbl if c.isalnum() or c == '_')
            headers, rows, err = run_sql(f"SELECT * FROM `{safe_tbl}` ORDER BY 1 DESC;")
            if err:
                self.send_response(500)
                self.end_headers()
                self.wfile.write(f"Error: {err}".encode("utf-8"))
                return

            if fmt in ("excel", "xls", "xlsx"):
                xml_data = build_excel_xml(safe_tbl, headers, rows)
                self.send_response(200)
                self.send_header("Content-Type", "application/vnd.ms-excel; charset=utf-8")
                self.send_header("Content-Disposition", f"attachment; filename={safe_tbl}_export.xls")
                self.end_headers()
                self.wfile.write(xml_data.encode("utf-8"))
                return

            self.send_response(200)
            self.send_header("Content-Type", "text/csv; charset=utf-8")
            self.send_header("Content-Disposition", f"attachment; filename={safe_tbl}_export.csv")
            self.end_headers()

            out = io.StringIO()
            out.write('\ufeff')  # UTF-8 BOM
            writer = csv.writer(out)
            writer.writerow(headers)
            writer.writerows(rows)
            self.wfile.write(out.getvalue().encode("utf-8"))
            return

        else:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"404 Not Found")

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        # Login Endpoint
        if path == "/api/login":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length).decode("utf-8")
            try:
                data = json.loads(body)
                u = data.get("username", "").strip()
                p = data.get("password", "").strip()

                user = authenticate_user(u, p)
                if not user:
                    self.send_json({"error": "Geçersiz kullanıcı adı veya şifre!"}, 401)
                    return

                token = uuid.uuid4().hex
                SESSIONS[token] = {**user, "expires": time.time() + 86400}
                log_audit(user["username"], user["role"], "Oturum açıldı")

                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Set-Cookie", f"neta_session={token}; Path=/; HttpOnly; SameSite=Lax; Max-Age=86400")
                self.end_headers()
                self.wfile.write(json.dumps({"success": True, "user": user}, ensure_ascii=False).encode("utf-8"))
            except Exception as e:
                self.send_json({"error": str(e)}, 400)
            return

        # Logout Endpoint
        elif path == "/api/logout":
            user = get_session_user(self)
            if user:
                log_audit(user["username"], user["role"], "Oturum kapatıldı")
            cookie_header = self.headers.get("Cookie", "")
            cookies = SimpleCookie()
            try:
                cookies.load(cookie_header)
                if "neta_session" in cookies:
                    token = cookies["neta_session"].value
                    if token in SESSIONS:
                        del SESSIONS[token]
            except Exception:
                pass

            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Set-Cookie", "neta_session=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT")
            self.end_headers()
            self.wfile.write(json.dumps({"success": True}).encode("utf-8"))
            return

        # Check Auth for protected POST endpoints
        user = get_session_user(self)
        if not user:
            self.send_json({"error": "Oturum açmanız gerekiyor."}, 401)
            return

        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode("utf-8")
        try:
            data = json.loads(body) if body else {}
        except Exception:
            data = {}

        # Create User (Admin Only)
        if path == "/api/users/create":
            if user["role"] != "ADMIN":
                self.send_json({"error": "Bu işlem için YÖNETİCİ (ADMIN) yetkisi gereklidir!"}, 403)
                return
            u = data.get("username", "").strip().replace("'", "")
            fn = data.get("full_name", "").strip().replace("'", "''")
            p = data.get("password", "").strip().replace("'", "''")
            r = "ADMIN" if data.get("role") == "ADMIN" else "OPERATOR"

            if not u or not p:
                self.send_json({"error": "Kullanıcı adı ve şifre zorunludur!"}, 400)
                return

            _, _, err = run_sql(f"INSERT INTO users (username, password_hash, full_name, role, is_active) VALUES ('{u}', '{p}', '{fn}', '{r}', 1);")
            if err:
                self.send_json({"error": f"Kullanıcı eklenemedi: {err}"}, 400)
                return
            log_audit(user["username"], user["role"], f"Yeni kullanıcı oluşturuldu: {u} ({r})")
            self.send_json({"success": True})
            return

        # Update User Full Profile via Pop-Up Modal (Admin Only)
        elif path == "/api/users/update":
            if user["role"] != "ADMIN":
                self.send_json({"error": "Bu işlem için YÖNETİCİ (ADMIN) yetkisi gereklidir!"}, 403)
                return
            uid = safe_int(data.get("id", 0))
            u = data.get("username", "").strip().replace("'", "")
            fn = data.get("full_name", "").strip().replace("'", "''")
            r = "ADMIN" if data.get("role") == "ADMIN" else "OPERATOR"
            act = 1 if str(data.get("is_active", 1)) == "1" else 0
            p = data.get("password", "").strip().replace("'", "''")

            if uid <= 0 or not fn:
                self.send_json({"error": "Kullanıcı ID ve Ad Soyad zorunludur!"}, 400)
                return

            _, r_existing, _ = run_sql(f"SELECT username FROM users WHERE id = {uid};")
            if not r_existing:
                self.send_json({"error": "Kullanıcı bulunamadı!"}, 404)
                return
            orig_username = r_existing[0][0]
            if orig_username.lower() == "admin":
                u = "admin"
                r = "ADMIN"
                act = 1
            elif not u:
                u = orig_username

            if p:
                sql = (
                    f"UPDATE users SET username = '{u}', full_name = '{fn}', "
                    f"role = '{r}', is_active = {act}, password_hash = '{p}' WHERE id = {uid};"
                )
            else:
                sql = (
                    f"UPDATE users SET username = '{u}', full_name = '{fn}', "
                    f"role = '{r}', is_active = {act} WHERE id = {uid};"
                )
            _, _, err = run_sql(sql)
            if err:
                self.send_json({"error": f"Kullanıcı güncellenemedi: {err}"}, 400)
                return
            log_audit(user["username"], user["role"], f"User updated via pop-up: {u} ({r}, active={act})")
            self.send_json({"success": True})
            return

        # Update User Password (Admin Only)
        elif path == "/api/users/update-password":
            if user["role"] != "ADMIN":
                self.send_json({"error": "Bu işlem için YÖNETİCİ (ADMIN) yetkisi gereklidir!"}, 403)
                return
            uid = int(data.get("id", 0))
            p = data.get("password", "").strip().replace("'", "''")
            if uid <= 0 or not p:
                self.send_json({"error": "Geçersiz parametreler!"}, 400)
                return
            _, _, err = run_sql(f"UPDATE users SET password_hash = '{p}' WHERE id = {uid};")
            if err:
                self.send_json({"error": err}, 400)
                return
            log_audit(user["username"], user["role"], f"Kullanıcı şifresi güncellendi (#ID: {uid})")
            self.send_json({"success": True})
            return

        # Toggle User Active (Admin Only)
        elif path == "/api/users/toggle":
            if user["role"] != "ADMIN":
                self.send_json({"error": "Bu işlem için YÖNETİCİ (ADMIN) yetkisi gereklidir!"}, 403)
                return
            uid = int(data.get("id", 0))
            act = 1 if data.get("is_active") == 1 else 0
            if uid <= 0:
                self.send_json({"error": "Geçersiz ID"}, 400)
                return
            run_sql(f"UPDATE users SET is_active = {act} WHERE id = {uid};")
            log_audit(user["username"], user["role"], f"Kullanıcı aktiflik durumu değiştirildi (#ID: {uid} -> {act})")
            self.send_json({"success": True})
            return

        # Delete User (Admin Only)
        elif path == "/api/users/delete":
            if user["role"] != "ADMIN":
                self.send_json({"error": "Bu işlem için YÖNETİCİ (ADMIN) yetkisi gereklidir!"}, 403)
                return
            uid = int(data.get("id", 0))
            if uid <= 0:
                self.send_json({"error": "Geçersiz ID"}, 400)
                return
            _, r, _ = run_sql(f"SELECT username FROM users WHERE id = {uid};")
            if r and r[0][0].lower() == 'admin':
                self.send_json({"error": "Ana 'admin' hesabı silinemez!"}, 403)
                return
            run_sql(f"DELETE FROM users WHERE id = {uid};")
            log_audit(user["username"], user["role"], f"Kullanıcı silindi (#ID: {uid})")
            self.send_json({"success": True})
            return

        # Change User Role (Admin Only)
        elif path == "/api/users/change-role":
            if user["role"] != "ADMIN":
                self.send_json({"error": "Bu işlem için YÖNETİCİ (ADMIN) yetkisi gereklidir!"}, 403)
                return
            uid = int(data.get("id", 0))
            new_role = data.get("role", "").strip().upper()
            if uid <= 0 or new_role not in ["ADMIN", "OPERATOR"]:
                self.send_json({"error": "Geçersiz yetki veya kullanıcı ID!"}, 400)
                return
            _, r, _ = run_sql(f"SELECT username FROM users WHERE id = {uid};")
            if not r:
                self.send_json({"error": "Kullanıcı bulunamadı!"}, 404)
                return
            target_username = r[0][0]
            if target_username.lower() == 'admin' and new_role != "ADMIN":
                self.send_json({"error": "Sistem ana yöneticisi (admin) hesabı operatör yapılamaz!"}, 403)
                return
            _, _, err = run_sql(f"UPDATE users SET role = '{new_role}' WHERE id = {uid};")
            if err:
                self.send_json({"error": err}, 400)
                return
            log_audit(user["username"], user["role"], f"Kullanıcı yetkisi değiştirildi: {target_username} -> {new_role}")
            self.send_json({"success": True})
            return

        # Create Satellite Profile (Admin Only)
        elif path == "/api/profiles/create":
            if user["role"] != "ADMIN":
                self.send_json({"error": "Bu işlem için YÖNETİCİ (ADMIN) yetkisi gereklidir!"}, 403)
                return
            name = data.get("name", "").strip().replace("'", "''")
            sat = data.get("satellite", "").strip().replace("'", "''")
            orb = data.get("orbital", "42.0E").strip().replace("'", "''")
            try:
                tol = float(data.get("tolerance", 1.5))
            except (ValueError, TypeError):
                tol = 1.50
            if not name or not sat:
                self.send_json({"error": "Profil adı ve uydu adı zorunludur!"}, 400)
                return
            _, _, err = run_sql(
                f"INSERT INTO satellite_profiles (profile_name, satellite_name, orbital_position, default_tolerance_db, is_active) "
                f"VALUES ('{name}', '{sat}', '{orb}', {tol:.2f}, 1);"
            )
            if err:
                self.send_json({"error": f"Profil oluşturulamadı: {err}"}, 400)
                return
            log_audit(user["username"], user["role"], f"Yeni uydu profili oluşturuldu: {name} ({sat} - {orb})")
            self.send_json({"success": True})
            return

        # Delete Satellite Profile (Admin Only)
        elif path == "/api/profiles/delete":
            if user["role"] != "ADMIN":
                self.send_json({"error": "Bu işlem için YÖNETİCİ (ADMIN) yetkisi gereklidir!"}, 403)
                return
            pid = int(data.get("id", 0))
            if pid <= 1:
                self.send_json({"error": "Varsayılan profil (#1) sistem tarafından korunmaktadır ve silinemez!"}, 403)
                return
            run_sql(f"DELETE FROM satellite_profiles WHERE id = {pid};")
            log_audit(user["username"], user["role"], f"Uydu profili silindi (#ID: {pid})")
            self.send_json({"success": True})
            return

        # Create/Update Channel in Profile (Admin Only)
        elif path == "/api/profiles/channels/create":
            if user["role"] != "ADMIN":
                self.send_json({"error": "Bu işlem için YÖNETİCİ (ADMIN) yetkisi gereklidir!"}, 403)
                return
            pid = int(data.get("profile_id", 0))
            ch_no = int(data.get("channel_no", 0))
            ch_name = data.get("channel_name", "").strip().replace("'", "''")
            freq = int(data.get("frequency_mhz", 0))
            sr = int(data.get("symbol_rate", 0))
            pol = data.get("polarization", "V").strip().upper()
            if pol not in ["V", "H"]:
                pol = "V"
            try:
                tol = float(data.get("tolerance_db", 1.5))
            except (ValueError, TypeError):
                tol = 1.50

            if pid <= 0 or ch_no <= 0 or not ch_name or freq <= 0 or sr <= 0:
                self.send_json({"error": "Lütfen frekans ve kanal parametrelerini eksiksiz giriniz!"}, 400)
                return

            sql = (
                f"INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) "
                f"VALUES ({pid}, {ch_no}, '{ch_name}', {freq}, {sr}, '{pol}', {tol:.2f}) "
                f"ON DUPLICATE KEY UPDATE "
                f"channel_name = '{ch_name}', frequency_mhz = {freq}, symbol_rate = {sr}, polarization = '{pol}', tolerance_db = {tol:.2f};"
            )
            _, _, err = run_sql(sql)
            if err:
                self.send_json({"error": f"Frekans kaydedilemedi: {err}"}, 400)
                return
            log_audit(user["username"], user["role"], f"Profil #{pid} kanal frekansı kaydedildi: Ch #{ch_no} ({ch_name} - {freq} MHz)")
            self.send_json({"success": True})
            return

        # Delete Channel from Profile (Admin Only)
        elif path == "/api/profiles/channels/delete":
            if user["role"] != "ADMIN":
                self.send_json({"error": "Bu işlem için YÖNETİCİ (ADMIN) yetkisi gereklidir!"}, 403)
                return
            pid = int(data.get("profile_id", 0))
            ch_no = int(data.get("channel_no", 0))
            if pid <= 0 or ch_no <= 0:
                self.send_json({"error": "Geçersiz parametreler!"}, 400)
                return
            run_sql(f"DELETE FROM profile_channels WHERE profile_id = {pid} AND channel_no = {ch_no};")
            log_audit(user["username"], user["role"], f"Profil #{pid} frekans kanalı silindi: Ch #{ch_no}")
            self.send_json({"success": True})
            return

        # Create Work Order
        elif path == "/api/work-orders/create":
            no = data.get("order_no", "").strip().replace("'", "''")
            cust = data.get("customer", "").strip().replace("'", "''")
            prod = data.get("product_code", "").strip().replace("'", "''")
            target = int(data.get("target", 100))
            _, _, err = run_sql(
                f"INSERT INTO work_orders (order_no, customer_name, product_code, target_quantity, status) "
                f"VALUES ('{no}', '{cust}', '{prod}', {target}, 'IN_PROGRESS');"
            )
            if err:
                self.send_json({"error": err}, 400)
                return
            log_audit(user["username"], user["role"], f"Yeni iş emri oluşturuldu: {no} ({target} Adet)")
            self.send_json({"success": True})
            return

        # SQL Query Console
        elif path == "/api/query":
            query = data.get("query", "").strip()
            if not query:
                self.send_json({"error": "Boş sorgu"}, 400)
                return

            if user["role"] != "ADMIN":
                destructive_keywords = ["DROP", "DELETE", "TRUNCATE", "ALTER", "UPDATE"]
                q_upper = query.upper()
                if any(kw in q_upper for kw in destructive_keywords):
                    self.send_json({"error": "Bu işlem için YÖNETİCİ (ADMIN) yetkisi gereklidir!"}, 403)
                    return

            headers, rows, err = run_sql(query)
            if err:
                self.send_json({"error": err}, 200)
            else:
                self.send_json({"headers": headers, "rows": rows, "error": None})
            return

        self.send_response(404)
        self.end_headers()

    def send_json(self, data, code=200):
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.end_headers()
        self.wfile.write(json.dumps(data, ensure_ascii=False).encode("utf-8"))

def main():
    global PORT
    if "--port" in sys.argv:
        try:
            idx = sys.argv.index("--port")
            if idx + 1 < len(sys.argv):
                PORT = int(sys.argv[idx + 1])
        except Exception:
            pass

    print("=" * 65)
    print("      NETA DRX9000 - ENTERPRISE FILO VE YONETIM PLATFORMU      ")
    print("=" * 65)
    print(f"MariaDB Sunucusu : {DB_HOST}:{DB_PORT} ({DB_NAME})")
    print(f"Web Arayuzu      : http://localhost:{PORT}")
    print(f"Baglanti Motoru  : {MYSQL_BIN}")
    print("Kimlik Dogrulama : AKTIF (users tablosu ile entegre)")
    print("=" * 65)

    ensure_mariadb_alive()
    ensure_database()

    print("\nDurdurmak icin Ctrl + C tuslarina basiniz.\n")

    server = HttpServerClass(("0.0.0.0", PORT), NetaWebHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[BILGI] Web sunucusu durduruldu.")
        server.server_close()

if __name__ == "__main__":
    main()
