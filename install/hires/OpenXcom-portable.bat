@echo off
rem OpenXcom Extended 8.7.1-hires-1 (unofficial hi-res overlay fork)
rem Portable launcher: data = this folder, user/config/saves = this folder\user
rem (keeps everything separate from any other OpenXcom install)
cd /d "%~dp0"
start "" "%~dp0openxcom.exe" -data "%~dp0." -user "%~dp0user"
