@echo off
cd /d "%~dp0"
if not exist deploy\igw_web (
  echo First build in Ubuntu, then copy the entire deploy folder here.
  pause
  exit /b 1
)
if exist E:\python3.11\python.exe (
  E:\python3.11\python.exe -m http.server 8019 --bind 192.168.31.100 --directory deploy
) else (
  py -3 -m http.server 8019 --bind 192.168.31.100 --directory deploy
)
pause
