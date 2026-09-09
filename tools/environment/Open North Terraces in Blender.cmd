@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0export_route1_pilot.ps1" -Recipe config/environment/route1_north_terraces.authoring.json -OpenBlender
