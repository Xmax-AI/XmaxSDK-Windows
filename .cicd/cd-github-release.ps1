param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Arguments)
$ErrorActionPreference = "Stop"
& python "$PSScriptRoot/workflow.py" release @Arguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
