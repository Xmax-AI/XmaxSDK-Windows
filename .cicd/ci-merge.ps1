param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Arguments)
$ErrorActionPreference = "Stop"
& python "$PSScriptRoot/workflow.py" merge @Arguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
