
set SolutionDir=%~1
set OutDir=%~2
set Config=%~3

set chakracore=%SolutionDir%node-chakracore\%Config%\chakracore.lib

if exist "%chakracore%" (
    echo node-chakracore passed.
) else (
    pushd "%SolutionDir%node-chakracore"
    call vcbuild.bat %Config% vs2019
    popd
)
copy /b "%chakracore%" "%OutDir%chakracore.lib"
copy /b "%OutDir%chakracore.lib"+,, "%OutDir%"