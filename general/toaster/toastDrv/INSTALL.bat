@echo off
:: Goto current directory
cd /d "%~dp0"

:: Install bus driver
devgen /add /bus ROOT /hardwareid "root\dynamicbus"
PNPUTIL /add-driver dynamicbus.inf /install

:: Install feature driver
PNPUTIL /add-driver wdf_featured.inf /install

:: Install filter driver
PNPUTIL /add-driver filter.inf /install

::pause
