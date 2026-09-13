@echo off
cd /d "%~dp0../.."
powershell -NoProfile -ExecutionPolicy Bypass -File tools/environment/preview_route1_pilot.ps1 -Recipe config/environment/route1_flat_experiment.authoring.json -Phase earthquake -Play
