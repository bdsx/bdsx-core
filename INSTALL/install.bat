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
if "%config%" == "Release" (
	call :copydll "%solutiondir%release\bdsx-core-%BDSX_CORE_VERSION%"
	call :zip "%solutiondir%release\bdsx-core-%BDSX_CORE_VERSION%"
)
goto :eof

:copydll
if not exist "%~1" mkdir "%~1"
copy "%outdir%Chakra.dll" "%~1\Chakra.dll"
copy "%outdir%VCRUNTIME140_1.dll" "%~1\VCRUNTIME140_1.dll"
copy "%outdir%VCRUNTIME140_1.pdb" "%~1\VCRUNTIME140_1.pdb"
copy "%outdir%ChakraCore.dll" "%~1\ChakraCore.dll"
copy "%outdir%pdbcachegen.exe" "%~1\pdbcachegen.exe"
EXIT /B 0

:zip
echo Zipping...
if exist "%~1.zip" del "%~1.zip"
call zip "%~1" "%~1.zip"
EXIT /B 0