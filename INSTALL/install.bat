set outdir=%~1
set config=%~2
set solutiondir=%~3

if "%config%" == "Debug" (
	set _debug=_debug
	set d=d
) 
call :copydll "%solutiondir%..\bdsx\bedrock_server"
if "%config%" == "Release" call :zip
goto :eof

:copydll
if not exist "%~1\mods" mkdir "%~1\mods"
copy "%outdir%bdsx.dll" "%~1\mods\bdsx.dll"
copy "%outdir%bdsx.pdb" "%~1\mods\bdsx.pdb"
copy "%outdir%Chakra.dll" "%~1\Chakra.dll"
copy "%outdir%ChakraCore.dll" "%~1\ChakraCore.dll"
copy "%solutiondir%INSTALL\dbghelp.dll" "%~1\dbghelp.dll"
EXIT /B 0

:zip
call "..\bdsx\version.bat"
call :copydll "%solutiondir%release\bdsx-core"
echo Zipping...
if exist "%solutiondir%release\bdsx-core-%BDSX_CORE_VERSION%.zip" del "%solutiondir%release\bdsx-core-%BDSX_CORE_VERSION%.zip"
call zip "%solutiondir%release\bdsx-core" "%solutiondir%release\bdsx-core-%BDSX_CORE_VERSION%.zip"
EXIT /B 0