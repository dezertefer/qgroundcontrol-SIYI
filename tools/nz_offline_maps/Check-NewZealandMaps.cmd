@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "BUNDLED_PYTHON=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if exist "%BUNDLED_PYTHON%" (
    "%BUNDLED_PYTHON%" "%SCRIPT_DIR%offline_map_preview.py"
    exit /b
)
where py >nul 2>nul
if %errorlevel% equ 0 (
    py -3 "%SCRIPT_DIR%offline_map_preview.py"
    exit /b
)
python "%SCRIPT_DIR%offline_map_preview.py"
endlocal
