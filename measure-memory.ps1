param(
    [string] $Executable = (Join-Path $PSScriptRoot 'build/lock_free_demo.exe')
)

$ErrorActionPreference = 'Stop'
$runs = 3
$intervalMs = 2
$bytesPerMb = 1000000
$culture = [Globalization.CultureInfo]::InvariantCulture

$startInfo = [Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Executable).Path)
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true

$totalAverageMb = 0.0
Write-Output "Process.WorkingSet64 | $intervalMs ms sleep | 1 MB = $bytesPerMb bytes"

for ($run = 1; $run -le $runs; $run++) {
    $process = [Diagnostics.Process]::Start($startInfo)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $sumBytes = 0.0
        $samples = 0

        while (-not $process.HasExited) {
            $process.Refresh()
            try {
                $bytes = $process.WorkingSet64
            }
            catch {
                if (-not $process.HasExited) { throw }
                break
            }
            if ($bytes -gt 0) {
                $sumBytes += $bytes
                $samples++
            }
            Start-Sleep -Milliseconds $intervalMs
        }

        $process.WaitForExit()
        Write-Output ""
        Write-Output "Run $run"
        Write-Output $stdout.GetAwaiter().GetResult().TrimEnd()
        if ($process.ExitCode -ne 0) {
            throw "Run $run failed (exit code $($process.ExitCode)). $($stderr.GetAwaiter().GetResult())"
        }
        if ($samples -eq 0) {
            throw "Run $run produced no memory samples."
        }

        $averageMb = $sumBytes / $samples / $bytesPerMb
        $totalAverageMb += $averageMb
        [string]::Format($culture, 'Average working set: {0:F2} MB ({1} samples)', $averageMb, $samples)
    }
    finally {
        $process.Dispose()
    }
}

Write-Output ""
[string]::Format($culture, 'Mean across all runs: {0:F2} MB', ($totalAverageMb / $runs))
