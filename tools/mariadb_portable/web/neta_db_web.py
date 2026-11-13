#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NETA DRX9000 - Portable MariaDB Web Management Dashboard
Author: Semi Eren Gökyıldız
Copyright: (C) 2026 NETA Elektronik A.Ş.

Zero-dependency web database console powered by Python standard library and MariaDB mysql.exe.
"""

import sys
import os
import json
import subprocess
import csv
import io
import urllib.parse
from http.server import HTTPServer, BaseHTTPRequestHandler

PORT = 8080
DB_HOST = "127.0.0.1"
DB_PORT = "3306"
DB_USER = "root"
DB_PASS = "neta123"
DB_NAME = "neta_antenna_test"

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
PORTABLE_DIR = os.path.abspath(os.path.join(BASE_DIR, ".."))
MYSQL_BIN = os.path.join(PORTABLE_DIR, "bin", "mysql.exe")

if not os.path.exists(MYSQL_BIN):
    # Fallback to PATH or common locations
    MYSQL_BIN = "mysql"

def run_sql(query, database=DB_NAME):
    """Executes an SQL query via mysql.exe and returns (columns, rows, error)"""
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
            # Clean password warning from stderr
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

def ensure_database():
    """Ensures neta_antenna_test database and initial schema exist"""
    # 1. Create database if not exists
    _, _, err = run_sql("CREATE DATABASE IF NOT EXISTS `neta_antenna_test` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;", database=None)
    if err:
        print(f"[UYARI] Veritabani olusturma kontrolu: {err}")
        return

    # 2. Check if tables exist
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

HTML_PAGE = r"""<!DOCTYPE html>
<html lang="tr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>NETA DRX9000 - Veritabanı Web Konsolu</title>
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
        /* Sidebar */
        aside {
            width: 280px;
            background-color: var(--bg-card);
            border-right: 1px solid var(--border);
            display: flex;
            flex-direction: column;
            flex-shrink: 0;
        }
        .brand {
            padding: 20px;
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
            font-size: 14px;
            font-weight: bold;
            color: var(--primary-light);
            line-height: 1.2;
        }
        .brand-sub {
            font-size: 11px;
            color: var(--text-muted);
        }
        .nav-section {
            padding: 14px 20px 6px;
            font-size: 11px;
            font-weight: bold;
            text-transform: uppercase;
            color: var(--text-muted);
            letter-spacing: 0.5px;
        }
        .table-list {
            list-style: none;
            overflow-y: auto;
            flex: 1;
            padding: 6px 10px;
        }
        .table-item {
            padding: 10px 14px;
            margin-bottom: 4px;
            border-radius: 6px;
            cursor: pointer;
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-size: 13px;
            transition: all 0.2s;
            color: var(--text-main);
        }
        .table-item:hover {
            background-color: var(--bg-hover);
        }
        .table-item.active {
            background-color: var(--primary);
            color: var(--bg-dark);
            font-weight: bold;
        }
        .table-badge {
            font-size: 11px;
            padding: 2px 7px;
            border-radius: 10px;
            background-color: rgba(255,255,255,0.1);
        }
        .table-item.active .table-badge {
            background-color: rgba(0,0,0,0.25);
            color: var(--bg-dark);
        }
        .sidebar-footer {
            padding: 16px;
            border-top: 1px solid var(--border);
            font-size: 11px;
            color: var(--text-muted);
            display: flex;
            flex-direction: column;
            gap: 6px;
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
            padding: 14px 24px;
            border-bottom: 1px solid var(--border);
            background-color: var(--bg-card);
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .view-tabs {
            display: flex;
            gap: 8px;
        }
        .tab-btn {
            background: none;
            border: 1px solid var(--border);
            color: var(--text-muted);
            padding: 7px 16px;
            border-radius: 6px;
            cursor: pointer;
            font-size: 12px;
            font-weight: bold;
            transition: 0.2s;
        }
        .tab-btn:hover { color: var(--text-main); border-color: var(--primary-light); }
        .tab-btn.active {
            background-color: var(--primary);
            color: var(--bg-dark);
            border-color: var(--primary);
        }
        .header-actions {
            display: flex;
            align-items: center;
            gap: 12px;
        }
        .search-input {
            background-color: var(--bg-dark);
            border: 1px solid var(--border);
            color: var(--text-main);
            padding: 7px 12px;
            border-radius: 6px;
            font-size: 12px;
            width: 220px;
        }
        .search-input:focus { outline: none; border-color: var(--primary-light); }
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
        /* View Content */
        .content-area {
            flex: 1;
            overflow: auto;
            padding: 20px 24px;
        }
        .card {
            background-color: var(--bg-card);
            border: 1px solid var(--border);
            border-radius: 8px;
            overflow: hidden;
            display: flex;
            flex-direction: column;
            height: 100%;
        }
        .card-header {
            padding: 12px 18px;
            border-bottom: 1px solid var(--border);
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-size: 13px;
            font-weight: bold;
            color: var(--primary-light);
        }
        .table-responsive {
            flex: 1;
            overflow: auto;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            font-size: 12px;
            text-align: left;
        }
        th {
            background-color: #0c251c;
            color: var(--primary-light);
            padding: 10px 14px;
            position: sticky;
            top: 0;
            border-bottom: 1px solid var(--border);
            font-weight: 600;
            white-space: nowrap;
        }
        td {
            padding: 9px 14px;
            border-bottom: 1px solid rgba(26, 61, 43, 0.5);
            white-space: nowrap;
            color: var(--text-main);
        }
        tr:hover td {
            background-color: rgba(18, 51, 38, 0.4);
        }
        .badge {
            padding: 3px 8px;
            border-radius: 4px;
            font-size: 11px;
            font-weight: bold;
            text-transform: uppercase;
        }
        .badge-pass { background-color: var(--pass-bg); color: var(--pass-text); border: 1px solid rgba(52,211,153,0.3); }
        .badge-fail { background-color: var(--fail-bg); color: var(--fail-text); border: 1px solid rgba(255,107,107,0.3); }
        .badge-role { background-color: #1E293B; color: #94A3B8; }
        /* SQL Console View */
        .sql-container {
            display: flex;
            flex-direction: column;
            gap: 14px;
            height: 100%;
        }
        .sql-editor {
            width: 100%;
            height: 140px;
            background-color: #050E0C;
            border: 1px solid var(--border);
            color: #A7F3D0;
            font-family: 'Consolas', monospace;
            font-size: 13px;
            padding: 14px;
            border-radius: 6px;
            resize: vertical;
        }
        .sql-editor:focus { outline: none; border-color: var(--primary-light); }
        .quick-queries {
            display: flex;
            gap: 8px;
            flex-wrap: wrap;
        }
        .quick-btn {
            background-color: rgba(255,255,255,0.05);
            border: 1px solid var(--border);
            color: var(--text-muted);
            padding: 4px 10px;
            border-radius: 4px;
            font-size: 11px;
            cursor: pointer;
        }
        .quick-btn:hover { color: var(--primary-light); border-color: var(--primary-light); }
        .error-box {
            background-color: var(--fail-bg);
            color: var(--fail-text);
            border: 1px solid rgba(255,107,107,0.3);
            padding: 12px 16px;
            border-radius: 6px;
            font-size: 12px;
            white-space: pre-wrap;
            font-family: monospace;
        }
        .success-box {
            background-color: var(--pass-bg);
            color: var(--pass-text);
            border: 1px solid rgba(52,211,153,0.3);
            padding: 12px 16px;
            border-radius: 6px;
            font-size: 12px;
        }
    </style>
</head>
<body>
    <aside>
        <div class="brand">
            <span class="brand-badge">NETA</span>
            <div>
                <div class="brand-title">DRX9000 WEB DB</div>
                <div class="brand-sub">MariaDB Yönetim Konsolu</div>
            </div>
        </div>
        <div class="nav-section">Üretim Tabloları</div>
        <ul class="table-list" id="tableList">
            <li class="table-item" style="color:var(--text-muted);">Yükleniyor...</li>
        </ul>
        <div class="sidebar-footer">
            <div><span class="status-dot"></span>MariaDB: <b>127.0.0.1:3306</b></div>
            <div>Veritabanı: <b>neta_antenna_test</b></div>
            <div>Geliştirici: <b>Semi Eren Gökyıldız</b></div>
        </div>
    </aside>

    <main>
        <header>
            <div class="view-tabs">
                <button class="tab-btn active" id="btnTabTable" onclick="switchTab('table')">📋 Tablo Verileri</button>
                <button class="tab-btn" id="btnTabSql" onclick="switchTab('sql')">⚡ SQL Konsolu</button>
            </div>
            <div class="header-actions">
                <input type="text" id="filterInput" class="search-input" placeholder="Bu tabloda ara..." onkeyup="filterTable()">
                <button class="btn btn-outline" onclick="exportCsv()">📥 CSV İndir</button>
                <button class="btn btn-primary" onclick="refreshCurrentTable()">🔄 Yenile</button>
            </div>
        </header>

        <div class="content-area">
            <!-- Table View -->
            <div id="viewTable" class="card">
                <div class="card-header">
                    <span id="currentTableName">Tablo Seçiniz</span>
                    <span id="recordCount" style="font-size:11px; color:var(--text-muted);">0 Kayıt</span>
                </div>
                <div class="table-responsive">
                    <table id="dataTable">
                        <thead><tr id="tableHead"><th>Bilgi</th></tr></thead>
                        <tbody id="tableBody"><tr><td>Lütfen soldan bir tablo seçin.</td></tr></tbody>
                    </table>
                </div>
            </div>

            <!-- SQL View -->
            <div id="viewSql" class="sql-container" style="display:none;">
                <div class="quick-queries">
                    <span style="font-size:11px; color:var(--text-muted); align-self:center;">Hızlı Sorgular:</span>
                    <button class="quick-btn" onclick="setSql('SELECT * FROM test_runs ORDER BY id DESC LIMIT 50;')">Son 50 Test</button>
                    <button class="quick-btn" onclick="setSql('SELECT * FROM test_results ORDER BY id DESC LIMIT 100;')">Detaylı RF Sonuçları</button>
                    <button class="quick-btn" onclick="setSql('SELECT * FROM system_logs ORDER BY id DESC LIMIT 50;')">Son Sistem Logları</button>
                    <button class="quick-btn" onclick="setSql('SELECT * FROM satellite_profiles;')">Uydu Profilleri</button>
                    <button class="quick-btn" onclick="setSql('SHOW FULL PROCESSLIST;')">Aktif İstemci Bağlantıları</button>
                </div>
                <textarea id="sqlInput" class="sql-editor" placeholder="SQL sorgunuzu buraya yazın (Örn: SELECT * FROM test_runs LIMIT 10;)"></textarea>
                <div>
                    <button class="btn btn-primary" onclick="runCustomSql()">🚀 Sorguyu Çalıştır (F9)</button>
                </div>
                <div id="sqlStatus"></div>
                <div class="card" style="flex:1;">
                    <div class="card-header">
                        <span>Sorgu Çıktısı</span>
                        <span id="sqlResultCount" style="font-size:11px; color:var(--text-muted);"></span>
                    </div>
                    <div class="table-responsive">
                        <table id="sqlResultTable">
                            <thead><tr id="sqlHead"></tr></thead>
                            <tbody id="sqlBody"></tbody>
                        </table>
                    </div>
                </div>
            </div>
        </div>
    </main>

    <script>
        let currentTable = 'test_runs';
        let fullTableData = { headers: [], rows: [] };

        async function fetchTables() {
            try {
                const res = await fetch('/api/tables');
                const data = await res.json();
                const list = document.getElementById('tableList');
                list.innerHTML = '';
                data.tables.forEach(t => {
                    const li = document.createElement('li');
                    li.className = `table-item ${t.name === currentTable ? 'active' : ''}`;
                    li.innerHTML = `<span>${t.name}</span><span class="table-badge">${t.rows}</span>`;
                    li.onclick = () => loadTable(t.name);
                    list.appendChild(li);
                });
            } catch (err) {
                console.error(err);
            }
        }

        async function loadTable(tableName) {
            currentTable = tableName;
            document.querySelectorAll('.table-item').forEach(el => {
                el.classList.toggle('active', el.querySelector('span').textContent === tableName);
            });
            document.getElementById('currentTableName').textContent = `Tablo: ${tableName}`;
            
            try {
                const res = await fetch(`/api/data?table=${encodeURIComponent(tableName)}`);
                const data = await res.json();
                if (data.error) {
                    alert("Hata: " + data.error);
                    return;
                }
                fullTableData = data;
                renderTable(data.headers, data.rows);
            } catch (err) {
                console.error(err);
            }
        }

        function renderTable(headers, rows) {
            const headTr = document.getElementById('tableHead');
            const tbody = document.getElementById('tableBody');
            headTr.innerHTML = '';
            tbody.innerHTML = '';

            headers.forEach(h => {
                const th = document.createElement('th');
                th.textContent = h;
                headTr.appendChild(th);
            });

            document.getElementById('recordCount').textContent = `${rows.length} Kayıt`;

            rows.forEach(r => {
                const tr = document.createElement('tr');
                r.forEach((val, idx) => {
                    const td = document.createElement('td');
                    const colName = headers[idx].toLowerCase();
                    if (val === 'PASS') {
                        td.innerHTML = '<span class="badge badge-pass">PASS</span>';
                    } else if (val === 'FAIL') {
                        td.innerHTML = '<span class="badge badge-fail">FAIL</span>';
                    } else if (colName.includes('role')) {
                        td.innerHTML = `<span class="badge badge-role">${val}</span>`;
                    } else {
                        td.textContent = val !== null ? val : 'NULL';
                    }
                    tr.appendChild(td);
                });
                tbody.appendChild(tr);
            });
        }

        function filterTable() {
            const query = document.getElementById('filterInput').value.toLowerCase();
            if (!query) {
                renderTable(fullTableData.headers, fullTableData.rows);
                return;
            }
            const filtered = fullTableData.rows.filter(row => 
                row.some(cell => String(cell).toLowerCase().includes(query))
            );
            renderTable(fullTableData.headers, filtered);
        }

        function refreshCurrentTable() {
            fetchTables();
            loadTable(currentTable);
        }

        function switchTab(tab) {
            document.getElementById('btnTabTable').classList.toggle('active', tab === 'table');
            document.getElementById('btnTabSql').classList.toggle('active', tab === 'sql');
            document.getElementById('viewTable').style.display = tab === 'table' ? 'flex' : 'none';
            document.getElementById('viewSql').style.display = tab === 'sql' ? 'flex' : 'none';
        }

        function setSql(q) {
            document.getElementById('sqlInput').value = q;
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
                    statusDiv.innerHTML = `<div class="error-box">❌ ${data.error}</div>`;
                    document.getElementById('sqlHead').innerHTML = '';
                    document.getElementById('sqlBody').innerHTML = '';
                    document.getElementById('sqlResultCount').textContent = '';
                    return;
                }
                statusDiv.innerHTML = `<div class="success-box">✔ Sorgu başarıyla çalıştırıldı!</div>`;
                document.getElementById('sqlResultCount').textContent = `${data.rows.length} Satır`;

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
                statusDiv.innerHTML = `<div class="error-box">❌ Bağlantı hatası: ${err.message}</div>`;
            }
        }

        document.getElementById('sqlInput').addEventListener('keydown', function(e) {
            if (e.key === 'F9' || (e.ctrlKey && e.key === 'Enter')) {
                e.preventDefault();
                runCustomSql();
            }
        });

        function exportCsv() {
            window.location.href = `/api/export?table=${encodeURIComponent(currentTable)}`;
        }

        // Initialize
        fetchTables();
        loadTable(currentTable);
    </script>
</body>
</html>
"""

class NetaWebHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        qs = urllib.parse.parse_qs(parsed.query)

        if path == "/" or path == "/index.html":
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(HTML_PAGE.encode("utf-8"))
            return

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
            # Sanitize table name to prevent injection
            safe_tbl = "".join(c for c in tbl if c.isalnum() or c == '_')
            headers, rows, err = run_sql(f"SELECT * FROM `{safe_tbl}` ORDER BY 1 DESC LIMIT 300;")
            if err:
                self.send_json({"error": err}, 500)
                return
            self.send_json({"headers": headers, "rows": rows})
            return

        elif path == "/api/export":
            tbl = qs.get("table", ["test_runs"])[0]
            safe_tbl = "".join(c for c in tbl if c.isalnum() or c == '_')
            headers, rows, err = run_sql(f"SELECT * FROM `{safe_tbl}` ORDER BY 1 DESC;")
            if err:
                self.send_response(500)
                self.end_headers()
                self.wfile.write(f"Error: {err}".encode("utf-8"))
                return

            self.send_response(200)
            self.send_header("Content-Type", "text/csv; charset=utf-8")
            self.send_header("Content-Disposition", f"attachment; filename={safe_tbl}_export.csv")
            self.end_headers()

            out = io.StringIO()
            # UTF-8 BOM for Excel compatibility
            out.write('\ufeff')
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
        if parsed.path == "/api/query":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length).decode("utf-8")
            try:
                data = json.loads(body)
                query = data.get("query", "").strip()
                if not query:
                    self.send_json({"error": "Empty query"}, 400)
                    return
                headers, rows, err = run_sql(query)
                if err:
                    self.send_json({"error": err}, 200)
                else:
                    self.send_json({"headers": headers, "rows": rows, "error": None})
            except Exception as e:
                self.send_json({"error": str(e)}, 400)
            return

        self.send_response(404)
        self.end_headers()

    def send_json(self, data, code=200):
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.end_headers()
        self.wfile.write(json.dumps(data, ensure_ascii=False).encode("utf-8"))

def main():
    print("=" * 65)
    print("      NETA DRX9000 - MARIADB WEB YONETIM VE VERI KONSOLU       ")
    print("=" * 65)
    print(f"MariaDB Sunucusu : {DB_HOST}:{DB_PORT} ({DB_NAME})")
    print(f"Web Arayuzu      : http://localhost:{PORT}")
    print(f"Baglanti Motoru  : {MYSQL_BIN}")
    print("=" * 65)

    ensure_database()

    print("\nDurdurmak icin Ctrl + C tuslarina basiniz.\n")

    server = HTTPServer(("0.0.0.0", PORT), NetaWebHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[BILGI] Web sunucusu durduruldu.")
        server.server_close()

if __name__ == "__main__":
    main()
