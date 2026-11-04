@echo off
cd /d "%~dp0"
if exist "E:\python3.11\python.exe" (
  "E:\python3.11\python.exe" rs485_rw_test.py
) else (
  py -3 rs485_rw_test.py
)
pause
