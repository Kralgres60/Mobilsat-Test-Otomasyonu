# 🗄️ NETA DRX9000 Antenna Test Automation — Portable MariaDB and Database Architecture

> [!IMPORTANT]
> **Summary:** This document explains the **Portable MariaDB** database architecture, the `DbManager` C++ class, satellite profiles, **Golden Sample** tables, work orders, audit logging, and the **Algorithm 2** atomic distributed lock.

---

## 1. Portable MariaDB Infrastructure

The application includes a standalone portable MariaDB server in `tools/mariadb_portable/`:

- `bin/mysqld.exe`: Database server engine (`port=3306`, `bind-address=0.0.0.0`)
- `bin/mysql_install_db.exe`: Initializes system data tables automatically if `data/` does not exist on target machine.
- `bin/mysqladmin.exe`: Shuts down the server safely on application exit.
- `my.ini`: Configured with UTF-8 (`utf8mb4`) and InnoDB engine.
- Default installation directory: `{sd}\NETA\DRX9000_Antenna_Test` (`C:\NETA\DRX9000_Antenna_Test`), avoiding permission issues and allowing portable folder copying.

---

## 2. `DbManager` Connection Chain and SQLite Fallback

Managed by `DbManager` singleton (`src/database/db_manager.h` and `.cpp`):

1. **Driver Priority:**
   `QMYSQL` (primary) → `QODBC` → `QSQLITE` (`neta_local_fallback.db`).
2. **Automatic Local Startup:**
   If host is `127.0.0.1` and port `3306` is closed, `startPortableMariaDbIfPresent()` launches the portable MariaDB process.
3. **Seamless SQLite Safety Fallback:**
   If MariaDB is not reachable, the system automatically falls back to local SQLite (`neta_local_fallback.db`) so no test data is ever lost.
4. **UTF-8 Support:**
   Full `utf8mb4` character set support across MariaDB and Qt layers.

---

## 3. Database Schema Overview

```mermaid
erDiagram
    users {
        int id PK
        varchar username UK
        varchar password_hash
        varchar full_name
        enum role "ADMIN | OPERATOR"
        tinyint is_active
    }
    satellite_profiles {
        int id PK
        varchar profile_name UK
        varchar satellite_name
        varchar orbital_position
        decimal default_tolerance_db
        tinyint is_active
    }
    profile_channels {
        int id PK
        int profile_id FK
        tinyint channel_no
        varchar channel_name
        int frequency_mhz
        int symbol_rate
        enum polarization "V | H"
        decimal tolerance_db
        decimal golden_cn_db
        decimal golden_pow_dbm
        tinyint has_golden_sample
        timestamp golden_updated_at
    }
    work_orders {
        int id PK
        varchar order_no UK
        varchar customer_name
        varchar product_code
        int satellite_profile_id
        int target_quantity
        int completed_quantity
        int passed_quantity
        int failed_quantity
        enum status
    }
    test_runs {
        bigint id PK
        int work_order_id FK
        tinyint station_id
        int operator_id FK
        varchar tester_name
        varchar operator_username
        varchar antenna_sn
        int satellite_profile_id FK
        timestamp test_timestamp
        int duration_sec
        enum overall_result "PASS | FAIL | ABORTED"
        varchar fail_reason
        decimal avg_delta_db
        decimal max_delta_db
    }
    test_run_channels {
        bigint id PK
        bigint test_run_id FK
        tinyint channel_no
        varchar channel_name
        int frequency_mhz
        decimal dut_cn_avg
        decimal dut_pow_avg
        decimal ref_cn_avg
        decimal ref_pow_avg
        decimal delta_db
        varchar result
    }
    audit_logs {
        bigint id PK
        timestamp log_timestamp
        varchar action_type
        text details
        varchar operator_username
        int station_id
    }
    reference_lock {
        int id PK
        int locked_by_station
        int current_channel_no
        timestamp lock_acquired_at
        timestamp heartbeat_at
    }
```

---

## 4. Work Order Management

Administrators add and manage work orders from the Host dashboard (`HostWindow`):
- `order_no`: Unique order number (e.g. `NETA-2026-001`)
- `customer_name`: Client name
- `product_code`: Antenna product model
- `satellite_profile_id`: Assigned satellite profile
- `target_quantity`: Target production batch size
- `completed_quantity`: Processed antennas count
- `passed_quantity`: Approved antennas count
- `failed_quantity`: Rejected antennas count
- `status`: `PENDING`, `IN_PROGRESS`, `COMPLETED`

When a test run finishes on any Station, `incrementOrderCount(workOrderId, isPass)` automatically updates the counters in the database.

---

## 5. Audit Logging (`audit_logs`)

All critical events are recorded for traceability:
- `TEST_START`, `TEST_FINISHED`
- `ORDER_CREATED`, `ORDER_UPDATED`, `ORDER_STATUS_CHANGED`, `ORDER_DELETED`
- `PROFILE_SAVED`, `PROFILE_DELETED`
- `USER_LOGIN`, `USER_UPDATED`

Records can be viewed, filtered, and exported in `TestHistoryWidget` (Tab 3: Audit Logs).

---

## 6. Algorithm 2: Distributed Atomic Locking

In 3-PC mode (`NETA_ALGORITMA_2_THREE_PC`), where there is no Host PC, Station PCs coordinate access to the single Reference Antenna using the `reference_lock` table:

```sql
UPDATE reference_lock 
SET locked_by_station = :myStation,
    current_channel_no = 1,
    lock_acquired_at = NOW(),
    heartbeat_at = NOW()
WHERE id = 1 AND (locked_by_station = 0 OR heartbeat_at < NOW() - INTERVAL 90 SECOND);
```

If `rowCount() > 0`, the lock is granted. On test completion, `locked_by_station` is set back to 0.

---

## 7. Web-Based Database Management Console (Headless / Standalone MariaDB)

When MariaDB runs on a standalone server or in 3-PC architecture without a desktop Host PC, the database can be monitored and managed directly from any web browser on the network.

### 🚀 Starting the Web Console
Double-click:
👉 `tools/mariadb_portable/start_web_db.bat`

This script:
1. Verifies MariaDB is running on port 3306 (starts it automatically if stopped).
2. Starts the zero-dependency Python web server (`web/neta_db_web.py`) on port 8080.
3. Automatically launches the browser at `http://localhost:8080`.

### 🌐 Features
- **Unified Authentication & RBAC:** Protected by the same credentials as the desktop application (`users` table). Users log in via a secure login dialog before accessing any database view or API.
- **Audit Logging:** Every login, logout, and database write action is automatically recorded into the `system_logs` table (`WEB_CONSOLE` source).
- **Role-Based Safety (Admin vs Operator):** Operators get read-only table viewing and search, while write and destructive SQL queries require `ADMIN` role.
- **Table Data Viewer:** Real-time view of `test_runs`, `test_results`, `satellite_profiles`, `profile_channels`, `work_orders`, `users`, and `system_logs`.
- **Live Search & Filter:** Instant search across all columns in any table.
- **CSV Data Export:** One-click CSV export with UTF-8 BOM encoding for Excel.
- **SQL Query Console:** Run arbitrary custom SQL queries (`SELECT`, `UPDATE`, `INSERT`, `SHOW PROCESSLIST`) with F9 or Ctrl+Enter.
- **Cross-Device Access:** Can be accessed from any phone, tablet, or PC on the same factory network via `http://<SERVER_IP>:8080`.

