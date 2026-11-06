:: Builds DebugVision with Ninja; run from a "x64 Native Tools Command Prompt for VS 2022".
:: COPPERSPICE_DIR points to a release CopperSpice install, COPPERSPICE_DEBUG_DIR to a debug CopperSpice install.
:: When COPPERSPICE_DIR is set and COPPERSPICE_DEBUG_DIR is not, the debug install must be at %COPPERSPICE_DIR%-debug.
:: When neither is set, C:/copperspice and C:/copperspice-debug are used.
:: Each configuration is built only when its CopperSpice install is present:
::   release (RelWithDebInfo) -> build/release/install, debug -> build/debug/install
:: generate a build configuration: cmake -G <generator> -B <dir>
:: build a generated configuration: cmake --build [-t <target>]
:: note: keep this file CRLF, cmd's goto/label lookup is unreliable with LF line endings

@setlocal

:: strip a trailing / or \ from COPPERSPICE_DIR (the | marker keeps the substitutions valid when it is not set)
@set "DV_CS=%COPPERSPICE_DIR%|"
@set "DV_CS=%DV_CS:/|=|%"
@set "DV_CS=%DV_CS:\|=|%"
@set "COPPERSPICE_DIR=%DV_CS:|=%"

@if not "%COPPERSPICE_DIR%"=="" if "%COPPERSPICE_DEBUG_DIR%"=="" (
  set "COPPERSPICE_DEBUG_DIR=%COPPERSPICE_DIR%-debug"
  if not exist "%COPPERSPICE_DIR%-debug\*" (
    echo error: COPPERSPICE_DIR=%COPPERSPICE_DIR% is set, but its debug counterpart %COPPERSPICE_DIR%-debug does not exist; install a debug CopperSpice there or set COPPERSPICE_DEBUG_DIR
    goto :fail
  )
)

@if "%COPPERSPICE_DIR%"=="" set "COPPERSPICE_DIR=C:/copperspice"
@if "%COPPERSPICE_DEBUG_DIR%"=="" set "COPPERSPICE_DEBUG_DIR=C:/copperspice-debug"

@set DV_BUILD_RELEASE=0
@set DV_BUILD_DEBUG=0
@if exist "%COPPERSPICE_DIR%/cmake/CopperSpice/CopperSpiceLibraryTargets-release.cmake" set DV_BUILD_RELEASE=1
@if exist "%COPPERSPICE_DEBUG_DIR%/cmake/CopperSpice/CopperSpiceLibraryTargets-debug.cmake" set DV_BUILD_DEBUG=1

@if %DV_BUILD_RELEASE%==0 if %DV_BUILD_DEBUG%==0 (
  echo error: no CopperSpice install found: no release build in COPPERSPICE_DIR=%COPPERSPICE_DIR% and no debug build in COPPERSPICE_DEBUG_DIR=%COPPERSPICE_DEBUG_DIR%
  goto :fail
)
@if %DV_BUILD_DEBUG%==0 echo note: skipping the Debug build, no debug CopperSpice install in COPPERSPICE_DEBUG_DIR=%COPPERSPICE_DEBUG_DIR%
@if %DV_BUILD_RELEASE%==0 echo note: skipping the release build, no release CopperSpice install in COPPERSPICE_DIR=%COPPERSPICE_DIR%

:: CMakeLists.txt takes the CopperSpice install from the COPPERSPICE_DIR environment variable, so point it at the matching install per configuration
@set "DV_CS_RELEASE_DIR=%COPPERSPICE_DIR%"
@set "DV_CS_DEBUG_DIR=%COPPERSPICE_DEBUG_DIR%"

@if %DV_BUILD_DEBUG%==1 (
  set "COPPERSPICE_DIR=%DV_CS_DEBUG_DIR%"
  cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -B build/debug || goto :fail
  cmake --build build/debug -t install || goto :fail
  ctest --test-dir build/debug --output-on-failure || goto :fail
)

@if %DV_BUILD_RELEASE%==1 (
  set "COPPERSPICE_DIR=%DV_CS_RELEASE_DIR%"
  cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -B build/release || goto :fail
  cmake --build build/release -t install || goto :fail
  ctest --test-dir build/release --output-on-failure || goto :fail
)

@exit /b 0

:fail
@echo build.bat: FAILED
@exit /b 1
