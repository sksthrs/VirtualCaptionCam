@echo off
cd /d %~dp0

REM Even on 64bit windows, 32bit DLL is necessary for 32bit apps.
REM "/s" option is for not showing dialog window.
regsvr32.exe /s NMUniversalVCamFilter_32.dll

REM 64bit DLL is only for 64bit windows.
if "%PROCESSOR_ARCHITECTURE%" equ "AMD64" (
  regsvr32.exe /s NMUniversalVCamFilter_64.dll
)
