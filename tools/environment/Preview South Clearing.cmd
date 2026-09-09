@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0preview_route1_pilot.ps1" -Recipe config/environment/route1_south_clearing.authoring.json -Phase planning
