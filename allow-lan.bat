@echo off
setlocal
title AI Relay - allow LAN access on TCP 3344
net session >nul 2>&1
if errorlevel 1 (
  echo Requesting administrator privileges...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

echo Removing old block/allow rules named "ai-relay" ...
netsh advfirewall firewall delete rule name="ai-relay" >nul 2>&1

echo Adding inbound allow rule for TCP 3344 ...
netsh advfirewall firewall add rule name="AI Relay TCP 3344" dir=in action=allow protocol=TCP localport=3344 profile=any >nul
if errorlevel 1 (
  echo [FAILED] Could not add firewall rule.
) else (
  echo [OK] Inbound TCP 3344 is now allowed for all network profiles.
  echo Test from another device: http://10.10.18.103:3344/
)
echo.
pause
