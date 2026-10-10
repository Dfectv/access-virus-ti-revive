@echo off
rem Start Virus Control Center with the IOCTL emulation (vtiemu-pipe.dll) via vtilaunch.
"C:\vti\vtilaunch.exe" "C:\Program Files\Access Music\Virus TI\Common\Virus Control Center.exe" > "%TEMP%\vtilaunch.log" 2>&1
