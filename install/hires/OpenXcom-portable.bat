@echo off
rem OpenXcom Extended 8.7.1 + hi-res overlay (release-hires-1)
rem Portable launcher: data = this folder, user/config/saves = this folder\user
rem (keeps everything separate from any other OpenXcom install)
cd /d "%~dp0"
start "" "%~dp0openxcom.exe" -data "%~dp0." -user "%~dp0user"
