@echo off
:: Goto current directory
cd /d "%~dp0"

devcon /remove "HID\Vid_045E&Pid_0047"

pnputil /delete-driver firefly.inf /uninstall /reboot

pause
