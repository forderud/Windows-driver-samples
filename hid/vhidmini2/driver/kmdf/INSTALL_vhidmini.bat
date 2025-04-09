@echo off
:: Goto current directory
cd /d "%~dp0"

:: Trust driver certificate
certutil.exe -addstore root vhidmini.cer
certutil.exe -f -addstore trustedpublisher vhidmini.cer

:: Use PnpUtil for installation
PNPUTIL /add-driver vhidmini.inf /install

pause
