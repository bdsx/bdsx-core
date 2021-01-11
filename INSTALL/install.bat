set outdir=%~1
set config=%~2
set solutiondir=%~3

if "%config%" == "Debug" (
	set _debug=_debug
	set d=d
)

call :copydll "%solutiondir%bdsx-node"
goto :eof

:copydll
if not exist "%~1" mkdir "%~1"
copy "%outdir%Chakra.dll" "%~1\bedrock_server\Chakra.dll"
copy "%outdir%ChakraCore.dll" "%~1\bedrock_server\ChakraCore.dll"
copy "%outdir%bdsx.dll" "%~1\mods\bdsx.dll"
copy "%outdir%bdsx.pdb" "%~1\mods\bdsx.pdb"
copy "%solutiondir%INSTALL\vcruntime140_1.dll" "%~1\mods\vcruntime140_1.dll"
EXIT /B 0
