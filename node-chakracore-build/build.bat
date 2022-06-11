
set SolutionDir=%~1
set Config=%~2
set OutDir=%~3

set targetdir=%SolutionDir%node-chakracore\%Config%
set node=%targetdir%\node.lib

call :check_output
if %found%==1 (
	echo node-chakracore passed.
	type NUL > "%targetdir%\node-chakracore-done"
	exit /b
)

pushd "%SolutionDir%\node-chakracore"
call vcbuild.bat %Config% vs2022 cctest
popd
call :check_output
if %found%==1 (
	type NUL > "%targetdir%\node-chakracore-done"
)
exit /b

:check_output
if not exist "%targetdir%\lib\cares.lib" goto :notfound
if not exist "%targetdir%\lib\chakrashim.lib" goto :notfound
if not exist "%targetdir%\lib\http_parser.lib" goto :notfound
if not exist "%targetdir%\lib\icudata.lib" goto :notfound
if not exist "%targetdir%\lib\icui18n.lib" goto :notfound
if not exist "%targetdir%\lib\icustubdata.lib" goto :notfound
if not exist "%targetdir%\lib\icutools.lib" goto :notfound
if not exist "%targetdir%\lib\icuucx.lib" goto :notfound
if not exist "%targetdir%\lib\libuv.lib" goto :notfound
if not exist "%targetdir%\lib\nghttp2.lib" goto :notfound
if not exist "%targetdir%\lib\node.lib" goto :notfound
if not exist "%targetdir%\lib\openssl.lib" goto :notfound
if not exist "%targetdir%\lib\zlib.lib" goto :notfound
set found=1
exit /b
:notfound
set found=0
exit /b