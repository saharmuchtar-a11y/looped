# Execute a Python file inside the running Unreal editor via VibeUE MCP (curl transport).
# Usage: .\vibeue_exec.ps1 -PythonFile path\to\script.py -OutFile path\to\result.txt
param(
    [Parameter(Mandatory=$true)][string]$PythonFile,
    [string]$OutFile = ''
)
$ErrorActionPreference = 'Stop'
$uri = 'http://127.0.0.1:8088/mcp'
$tmp = Join-Path $env:TEMP 'vibeue_req.json'
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
function Write-Req([string]$Json) { [System.IO.File]::WriteAllText($tmp, $Json, $Utf8NoBom) }

# 1) initialize (fresh session each run keeps this stateless and robust)
Write-Req (@{jsonrpc='2.0'; id=1; method='initialize'; params=@{protocolVersion='2025-03-26'; capabilities=@{}; clientInfo=@{name='cursor-agent'; version='1.0'}}} |
    ConvertTo-Json -Depth 6 -Compress)
$initResp = curl.exe -s -D "$env:TEMP\vibeue_hdrs.txt" -X POST $uri -H 'Content-Type: application/json' -H 'Accept: application/json, text/event-stream' --data-binary "@$tmp"
$session = @(Get-Content "$env:TEMP\vibeue_hdrs.txt" | Where-Object { $_ -match '^Mcp-Session-Id:' } | ForEach-Object { ($_ -replace '^Mcp-Session-Id:\s*', '').Trim() }) | Select-Object -First 1
if (-not $session) { throw "No MCP session id. Init response: $initResp" }

# 2) initialized notification
Write-Req (@{jsonrpc='2.0'; method='notifications/initialized'} | ConvertTo-Json -Compress)
curl.exe -s -X POST $uri -H 'Content-Type: application/json' -H 'Accept: application/json, text/event-stream' -H "Mcp-Session-Id: $session" --data-binary "@$tmp" | Out-Null

# 3) tools/call execute_python_code
$code = [System.IO.File]::ReadAllText($PythonFile)
Write-Req (@{jsonrpc='2.0'; id=2; method='tools/call'; params=@{name='execute_python_code'; arguments=@{code=$code}}} |
    ConvertTo-Json -Depth 8 -Compress)
$resp = curl.exe -s --max-time 280 -X POST $uri -H 'Content-Type: application/json' -H 'Accept: application/json, text/event-stream' -H "Mcp-Session-Id: $session" --data-binary "@$tmp"

# Reassemble SSE payload
$payload = ($resp -split "`n" | Where-Object { $_ -match '^data: ' } | ForEach-Object { $_ -replace '^data: ', '' }) -join "`n"
if (-not $payload) { $payload = $resp }
if ($OutFile) {
    Set-Content -Path $OutFile -Value $payload -Encoding UTF8
    Write-Output "WROTE $OutFile ($($payload.Length) chars)"
} else {
    Write-Output $payload
}
