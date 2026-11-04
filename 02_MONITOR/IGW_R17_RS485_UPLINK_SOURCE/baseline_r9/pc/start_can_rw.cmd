@echo off
cd /d "%~dp0"
if exist E:\python3.11\python.exe (E:\python3.11\python.exe can_rw_test.py --mode can --count 1) else (py -3 can_rw_test.py --mode can --count 1)
pause
