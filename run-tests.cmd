@echo off
rem Runs all of FeedView's tests and shows which of your requests pass
rem (the list is in tests\REQUIREMENTS.md, the report in build\requirements-report.md).
rem Double-click it, or run it in a terminal; "run-tests -Quick" leaves out the tests that
rem take over the screen.
rem
rem The full run takes over the screen and the mouse for about two minutes: FeedView goes
rem fullscreen and the mouse moves by itself. Don't touch them until it has finished.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-tests.ps1" %*
set code=%errorlevel%
echo.
if %code%==0 (echo All tests passed.) else (echo Some tests FAILED - see above, and build\requirements-report.md)
rem Keep the window open when started by double-clicking.
echo %cmdcmdline% | findstr /i /c:"%~nx0" >nul && pause
exit /b %code%
