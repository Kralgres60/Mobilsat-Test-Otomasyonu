param(
    [Parameter(Mandatory = $true)]
    [int]$ParentPid,
    [Parameter(Mandatory = $true)]
    [string]$RootDir
)

$ErrorActionPreference = 'SilentlyContinue'

# Wait until the fabrika_test_panel.bat cmd.exe process exits (even if closed via X button)
try {
    $proc = Get-Process -Id $ParentPid -ErrorAction SilentlyContinue
    if ($proc) {
        $proc.WaitForExit()
    }
} catch {}

while (Get-Process -Id $ParentPid -ErrorAction SilentlyContinue) {
    Start-Sleep -Milliseconds 300
}

# 1. Stop application and simulator processes
Stop-Process -Name NETA_Antenna_Test, NETA_DRX_Simulator -Force -ErrorAction SilentlyContinue

# 2. Stop Web DB server (port 8888 + python script)
Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like '*neta_db_web.py*' } | ForEach-Object {
    Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
}
Get-NetTCPConnection -LocalPort 8888 -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.OwningProcess -gt 0) {
        Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue
    }
}

# 3. Gracefully shut down MariaDB with a bounded 2-second wait, then force-kill if still alive
$adminCandidates = @(
    (Join-Path $RootDir 'OUTPUT\Database\mariadb_portable\bin\mysqladmin.exe'),
    (Join-Path $RootDir 'tools\mariadb_portable\bin\mysqladmin.exe')
)
foreach ($adminExe in $adminCandidates) {
    if (Test-Path $adminExe) {
        try {
            $adminProc = Start-Process -FilePath $adminExe `
                -ArgumentList '-h', '127.0.0.1', '-P', '3306', '-u', 'root', '-pneta123', 'shutdown' `
                -WindowStyle Hidden -PassThru -ErrorAction SilentlyContinue
            if ($adminProc) {
                $null = $adminProc.WaitForExit(2000)
                if (-not $adminProc.HasExited) {
                    $adminProc.Kill()
                }
            }
        } catch {}
        break
    }
}

Start-Sleep -Milliseconds 300
Stop-Process -Name mysqld -Force -ErrorAction SilentlyContinue
& taskkill.exe /F /T /IM mysqld.exe 2>$null | Out-Null

# 4. Clean up helper cmd.exe windows spawned for DB or Web DB
Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq 'cmd.exe' -and (
        $_.CommandLine -like '*start_db.bat*' -or
        $_.CommandLine -like '*NETA MariaDB Server*' -or
        $_.CommandLine -like '*NETA MariaDB Engine*' -or
        $_.CommandLine -like '*NETA Web DB Server*'
    )
} | ForEach-Object {
    Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
}
