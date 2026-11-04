@echo off
cd /d "%~dp0"
echo This test writes volatile registers 16 and 31 on logical nodes 1 and 2.
echo Keep the R13 main gateway and slave running; close other write panels.
if exist E:\python3.11\python.exe (
  E:\python3.11\python.exe accept_multi.py --nodes 2
) else (
  py -3 accept_multi.py --nodes 2
)
pause
