@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>&1
if errorlevel 1 (
  python check_project.py
) else (
  py -3 check_project.py
)
if errorlevel 1 (
  echo CHECK FAILED. Keep the complete output for review.
  pause
  exit /b 1
)
echo Source checks passed. TD timing and Linux DT review are still pending.
pause
