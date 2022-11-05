set outdir=%~1
set config=%~2
set solutiondir=%~3

call "..\bdsx\version.bat"
set /p BDS_VERSION=<"../../bdsx/bdsx/version-bds.json"
set BDS_VERSION=%BDS_VERSION:"=%

if "%config%" == "Debug" (
	set _debug=_debug
	set d=d
) 

call :copydll "%solutiondir%..\bdsx\bedrock_server"
if "%config%" == "Release" goto :release
goto :eof

:release
call :copydll "%solutiondir%release\bdsx-core-%BDSX_CORE_VERSION%"
if %errorlevel% neq 0 goto :eof
call :zip "%solutiondir%release\bdsx-core-%BDSX_CORE_VERSION%"
goto :eof

:copydll
if not exist "%~1" mkdir "%~1"
copy "%outdir%VCRUNTIME140_1.dll" "%~1\VCRUNTIME140_1.dll"
if %errorlevel% neq 0 goto :eof
copy "%outdir%VCRUNTIME140_1.pdb" "%~1\VCRUNTIME140_1.pdb"
if %errorlevel% neq 0 goto :eof
copy "%outdir%ChakraCore.dll" "%~1\ChakraCore.dll"
if %errorlevel% neq 0 goto :eof
copy "%outdir%pdbcachegen.exe" "%~1\pdbcachegen.exe"
if %errorlevel% neq 0 goto :eof
EXIT /B 0

:zip
echo Zipping...
if exist "%~1.zip" del "%~1.zip"
call zip "%~1" "%~1.zip"
EXIT /B 0