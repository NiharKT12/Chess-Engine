@echo off
REM Builds and runs the engine regression tests (no raylib required).
REM Run from a Visual Studio Developer Command Prompt.
cd /d "%~dp0.."
cl /nologo /EHsc /O2 /std:c++17 /I"Chess Engine" ^
   tests\tests.cpp "Chess Engine\Board.cpp" "Chess Engine\MoveGen.cpp" "Chess Engine\Search.cpp" "Chess Engine\Zobrist.cpp" ^
   /Fe:"%TEMP%\chess_tests.exe" /Fo:"%TEMP%\\"
if errorlevel 1 exit /b 1
"%TEMP%\chess_tests.exe"
