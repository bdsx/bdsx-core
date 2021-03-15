set outdir=%~1
set config=%~2
set solutiondir=%~3

call "..\bdsx\version.bat"

if "%config%" == "Debug" (
	set _debug=_debug
	set d=d
) 
call :copydll "%solutiondir%..\bdsx\bedrock_server"
if "%config%" == "Release" call :zip
goto :eof

:copydll
if not exist "%~1" mkdir "%~1"
copy "%outdir%Chakra.dll" "%~1\Chakra.dll"
copy "%outdir%Chakra.pdb" "%~1\Chakra.pdb"
copy "%outdir%ChakraCore.dll" "%~1\ChakraCore.dll"
EXIT /B 0

:zip
call :copydll "%solutiondir%release\bdsx-core-%BDSX_CORE_VERSION%"
echo Zipping...
if exist "%solutiondir%release\bdsx-core-%BDSX_CORE_VERSION%.zip" del "%solutiondir%release\bdsx-core-%BDSX_CORE_VERSION%.zip"
call zip "%solutiondir%release\bdsx-core-%BDSX_CORE_VERSION%" "%solutiondir%release\bdsx-core-%BDSX_CORE_VERSION%.zip"
EXIT /B 0