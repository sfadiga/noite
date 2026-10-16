# Throwaway spike helper: a dedicated "previous foreground app" for run-trials.ps1.
# Any keystroke that misses the capture window lands in this text box and is
# written to $Out as it arrives. It closes itself when "$Out.stop" appears,
# so the harness never has to send a close keystroke to whatever is focused.
param([Parameter(Mandatory)][string]$Out)

Add-Type -AssemblyName System.Windows.Forms
$form = New-Object System.Windows.Forms.Form
$form.Text = "noite-harness-target"
$form.Width = 420
$form.Height = 220
$box = New-Object System.Windows.Forms.TextBox
$box.Multiline = $true
$box.Dock = "Fill"
$form.Controls.Add($box)
$form.ActiveControl = $box
[IO.File]::WriteAllText($Out, "")
$box.Add_TextChanged({ [IO.File]::WriteAllText($Out, $box.Text) })
$form.Add_Activated({ $box.Focus() | Out-Null })
# Launched minimized (so its console stays out of the way); restore the form itself.
$form.Add_Shown({
    $form.WindowState = "Normal"
    $form.Activate()
    $box.Focus() | Out-Null
    [IO.File]::WriteAllText("$Out.ready", "")
})
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 200
$timer.Add_Tick({ if (Test-Path "$Out.stop") { $timer.Stop(); $form.Close() } })
$timer.Start()
[void]$form.ShowDialog()
