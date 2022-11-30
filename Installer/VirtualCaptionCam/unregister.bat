@echo off
cd /d %~dp0

REM 32bit DLL exists on both 32bit and 64bit windows (32bit apps use 32bit DLLs)
REM "/s" option is for not showing dialog window.
regsvr32.exe /u /s NMUniversalVCamFilter_32.dll

REM 64bit DLL is only for 64bit windows.
if "%PROCESSOR_ARCHITECTURE%" equ "AMD64" (
  regsvr32.exe /u /s NMUniversalVCamFilter_64.dll
)
