@echo off
:: Goto current directory
cd /d "%~dp0"

:: DeCon does work
devcon.exe /r install firefly.inf "HID\Vid_045E&Pid_0047"

:: DevGen/pnputil doesn't seem to work
::devgen.exe /add /bus ROOT /hardwareid "HID\Vid_045E&Pid_0047"
::pnputil /add-driver firefly.inf /install /reboot

pause
