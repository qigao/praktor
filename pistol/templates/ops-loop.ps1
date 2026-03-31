# ops-loop.ps1 — Periodic workflow runner for Praktor ops workflows (Windows)
#
# Usage:
#   .\ops-loop.ps1 -Workflow monitor-host.yml
#   .\ops-loop.ps1 -Workflow monitor-host.yml -IntervalSeconds 30
#   .\ops-loop.ps1 -Workflow monitor-processes.yml -IntervalSeconds 10
#
# Environment:
#   $env:PRAKTOR_BIN     Path to praktor binary
#   $env:ALERT_WEBHOOK   Webhook URL for alerts
#   $env:OPS_LOG_DIR     Directory for log files

param(
    [Parameter(Mandatory = $true)]
    [string]$Workflow,

    [int]$IntervalSeconds = 60
)

$ErrorActionPreference = "Continue"

$PraktorBin = if ($env:PRAKTOR_BIN) { $env:PRAKTOR_BIN } else { "praktor" }
$LogDir = if ($env:OPS_LOG_DIR) { $env:OPS_LOG_DIR } else { ".\ops-logs" }

if (-not (Test-Path $LogDir)) {
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
}

Write-Host @"
+--------------------------------------------------------------+
|  Praktor Ops Loop (Windows)                                    |
|  Workflow : $Workflow
|  Interval : ${IntervalSeconds}s
|  Logs     : $LogDir
+--------------------------------------------------------------+
"@

$Iteration = 0

try {
    while ($true) {
        $Iteration++
        $Timestamp = Get-Date -Format "yyyy-MM-ddTHH:mm:ss"
        $LogFile = Join-Path $LogDir ("run_{0}_{1}.log" -f $Iteration, (Get-Date -Format "yyyyMMdd_HHmmss"))

        Write-Host "[$Timestamp] Iteration #$Iteration - running $Workflow"

        $process = Start-Process -FilePath $PraktorBin `
            -ArgumentList "-f", $Workflow, "--concurrent" `
            -NoNewWindow -Wait -PassThru `
            -RedirectStandardOutput $LogFile `
            -RedirectStandardError "$LogFile.err" 2>$null

        if ($process.ExitCode -eq 0) {
            Write-Host "[$Timestamp] OK  Completed successfully"
        }
        else {
            Write-Host "[$Timestamp] ERR Completed with failures (see $LogFile)"
        }

        # Merge stderr into main log
        if (Test-Path "$LogFile.err") {
            Get-Content "$LogFile.err" | Add-Content $LogFile
            Remove-Item "$LogFile.err" -Force
        }

        # Rotate: keep last 100 log files
        $logs = Get-ChildItem -Path $LogDir -Filter "run_*.log" | Sort-Object Name
        if ($logs.Count -gt 100) {
            $logs | Select-Object -First ($logs.Count - 100) | Remove-Item -Force
        }

        Start-Sleep -Seconds $IntervalSeconds
    }
}
finally {
    Write-Host ""
    Write-Host "[ops-loop] Shutting down after $Iteration iterations."
}
