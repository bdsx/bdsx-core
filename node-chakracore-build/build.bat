
set SolutionDir=%~1
set Config=%~2
set OutDir=%~3

set node=%SolutionDir%node-chakracore\%Config%\node.lib

call :check_output
if %found%==1 (
	echo node-chakracore passed.
	type NUL > "%OutDir%node-chakracore-done"
	goto :eof
)

pushd "%SolutionDir%node-chakracore"
call vcbuild.bat %Config% vs2019
popd
call :check_output
if %found%==1 (
	type NUL > "%OutDir%node-chakracore-done"
)

:check_output
if not exist "%SolutionDir%node-chakracore\%Config%\lib\cares.lib" goto :notfound
if not exist "%SolutionDir%node-chakracore\%Config%\lib\chakrashim.lib" goto :notfound
if not exist "%SolutionDir%node-chakracore\%Config%\lib\http_parser.lib" goto :notfound
if not exist "%SolutionDir%node-chakracore\%Config%\lib\icudata.lib" goto :notfound
if not exist "%SolutionDir%node-chakracore\%Config%\lib\icui18n.lib" goto :notfound
if not exist "%SolutionDir%node-chakracore\%Config%\lib\icustubdata.lib" goto :notfound
if not exist "%SolutionDir%node-chakracore\%Config%\lib\icutools.lib" goto :notfound
if not exist "%SolutionDir%node-chakracore\%Config%\lib\icuucx.lib" goto :notfound
if not exist "%SolutionDir%node-chakracore\%Config%\lib\libuv.lib" goto :notfound
if not exist "%SolutionDir%node-chakracore\%Config%\lib\nghttp2.lib" goto :notfound
if not exist "%SolutionDir%node-chakracore\%Config%\lib\node.lib" goto :notfound
if not exist "%SolutionDir%node-chakracore\%Config%\lib\openssl.lib" goto :notfound
if not exist "%SolutionDir%node-chakracore\%Config%\lib\zlib.lib" goto :notfound
set found=1
exit /b
:notfound
set found=0
exit /b