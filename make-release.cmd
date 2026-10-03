@echo off
rem Makes a FeedView release package to share: dist\FeedView-<version>-windows-x64.zip,
rem with only the app, its NDI runtime, the README, the licences and the Companion module.
rem It runs all the tests first (about two minutes; FeedView goes fullscreen and the mouse
rem moves by itself - don't touch them) and makes no package if they fail.
rem Options (from a terminal): -WithTestSender adds the test-pattern sender, -SkipTests skips the tests.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\make-release.ps1" %*
set code=%errorlevel%
echo.
if %code%==0 (echo Done: the package is in the dist folder.) else (echo No package was made - see above.)
rem When started by double-clicking: show the dist folder, keep the window open.
echo %cmdcmdline% | findstr /i /c:"%~nx0" >nul &&((if %code%==0 start "" "%~dp0dist") & pause)
exit /b %code%
