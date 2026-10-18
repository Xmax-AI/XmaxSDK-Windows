param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Arguments)
$ErrorActionPreference = "Stop"
& python "$PSScriptRoot/pipeline.py" build-windows @Arguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
