@echo OFF
setlocal enabledelayedexpansion
cd %cd%
set EXAMPLE_PROJECT_PATH=%cd%

rem --------------------------------------------------------------------------------------------------------
rem Set the color of the terminal to blue with yellow text
rem --------------------------------------------------------------------------------------------------------
COLOR 8E
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore Cyan Welcome I am your EXAMPLE PROJECT dependency updater bot, let me get to work...
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
echo.

:DOWNLOAD_DEPENDENCIES
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White EXAMPLE PROJECT - DOWNLOADING DEPENDENCIES
powershell write-host -fore White ------------------------------------------------------------------------------------------------------

echo.
rmdir "../dependencies/xcore" /S /Q
git clone https://gitlab.com/LIONant/xcore.git "../dependencies/xcore"
if %ERRORLEVEL% GEQ 1 goto :PAUSE

rmdir "../dependencies/glut" /S /Q
git clone https://github.com/markkilgard/glut.git "../dependencies/glut"
if %ERRORLEVEL% GEQ 1 goto :PAUSE


:FIND_VSTUDIO
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White EXAMPLE_PROJECT - FINDING VISUAL STUDIO / MSBuild
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
cd /d %EXAMPLE_PROJECT_PATH%
for /f "usebackq tokens=*" %%i in (`.\..\dependencies\xcore\bin\vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    SET MSBUILD=%%i
    GOTO :BREAK_OUT
)
:BREAK_OUT

for /f "usebackq tokens=1* delims=: " %%i in (`.\..\dependencies\xcore\bin\vswhere -latest -requires Microsoft.VisualStudio.Workload.NativeDesktop`) do (
    if /i "%%i"=="installationPath" set VSPATH=%%j
)

IF EXIST "%MSBUILD%" ( 
    echo OK 
    GOTO :COMPILATION
    )
echo Failed to find MSBuild!!! 
GOTO :ERROR

:COMPILATION
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White EXAMPLE PROJECT - COMPILING DEPENDENCIES
powershell write-host -fore White ------------------------------------------------------------------------------------------------------

cd ../dependencies/xcore/builds
call UpdateDependencies.bat "return"
if %ERRORLEVEL% GEQ 1 goto :PAUSE
cd /d %EXAMPLE_PROJECT_PATH%

powershell write-host -fore Cyan glut: Updating...
"%VSPATH%\Common7\IDE\devenv.exe" "%CD%\..\dependencies\glut\glut_2012.vcxproj" /upgrade
if %ERRORLEVEL% GEQ 1 goto :ERROR

echo.
powershell write-host -fore Cyan glut Release: Compiling...
"%MSBUILD%" "%CD%\..\dependencies\glut\glut_2012.vcxproj" /p:configuration=Release /p:Platform="x64" /verbosity:minimal 
if %ERRORLEVEL% GEQ 1 goto :ERROR

echo.
powershell write-host -fore Cyan glut Debug: Compiling...
"%MSBUILD%" "%CD%\..\dependencies\glut\glut_2012.vcxproj" /p:configuration=Debug /p:Platform="x64" /verbosity:minimal 
if %ERRORLEVEL% GEQ 1 goto :ERROR

:DONE
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White EXAMPLE PROJECT - DONE!!
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
goto :PAUSE

:ERROR
powershell write-host -fore Red ------------------------------------------------------------------------------------------------------
powershell write-host -fore Red EXAMPLE PROJECT - ERROR!!
powershell write-host -fore Red ------------------------------------------------------------------------------------------------------

:PAUSE
rem if no one give us any parameters then we will pause it at the end, else we are assuming that another batch file called us
if %1.==. pause
