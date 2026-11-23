@echo off
title PCScript
set "PATH=%~dp0;%PATH%"
echo.
echo   PCScript 2.0.0 - Cambridge pseudocode
echo.
echo   Run a program:    pcscript yourfile.pcs
echo   Try an example:   pcscript 01_variables.pcs
echo   Check for errors: pcscript --check yourfile.pcs
echo.
cmd /k
