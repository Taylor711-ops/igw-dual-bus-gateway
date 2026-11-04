@echo off
cd /d "%~dp0"
if exist "E:\python3.11\python.exe" (
    "E:\python3.11\python.exe" dual_panel.py
) else (
    py -3 dual_panel.py
)
pause
