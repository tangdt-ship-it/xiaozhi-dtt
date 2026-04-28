param(
  [string]$GatewayUrl = "http://127.0.0.1:8787",
  [string]$SongQuery = "Nơi Này Có Anh",
  [string]$DeviceId = "AA:BB:CC:DD:EE:FF",
  [string]$ClientId = "test-client"
)

$ErrorActionPreference = "Stop"

Write-Host "[1/3] Health check: $GatewayUrl/healthz"
$health = Invoke-RestMethod "$GatewayUrl/healthz"
$health | ConvertTo-Json -Depth 5

Write-Host "[2/3] Resolve local song via /v1/music/play"
$payload = @{
  provider  = "local_vn"
  query     = $SongQuery
  device_id = $DeviceId
  client_id = $ClientId
} | ConvertTo-Json -Compress

$result = Invoke-RestMethod `
  -Uri "$GatewayUrl/v1/music/play" `
  -Method Post `
  -ContentType "application/json; charset=utf-8" `
  -Body ([System.Text.Encoding]::UTF8.GetBytes($payload))

$result | ConvertTo-Json -Depth 6

$streamUrl = $result.stream_url
if (-not $streamUrl) {
  throw "No stream_url returned from gateway."
}

Write-Host "[3/3] Opening stream URL in default player/browser: $streamUrl"
Start-Process $streamUrl
