param([Parameter(Mandatory=$true)][string]$ScreenSaver)

Add-Type -AssemblyName System.Windows.Forms
$form = New-Object System.Windows.Forms.Form
$form.Text = '3D Maze Preview'
$form.ClientSize = New-Object System.Drawing.Size(640, 400)
$form.StartPosition = 'CenterScreen'
$form.Show()
$form.Refresh()
$handle = [int64]$form.Handle
$processInfo = [System.Diagnostics.ProcessStartInfo]::new()
$processInfo.FileName = (Resolve-Path -LiteralPath $ScreenSaver).Path
$processInfo.Arguments = "/p $handle"
$processInfo.UseShellExecute = $false
$process = [System.Diagnostics.Process]::Start($processInfo)
try {
    while ($form.Visible -and -not $process.HasExited) {
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 20
    }
} finally {
    if (-not $process.HasExited) { $process.Kill() }
    $form.Dispose()
    $process.Dispose()
}
