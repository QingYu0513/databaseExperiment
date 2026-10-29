@echo off
rem Runs OUTSIDE the DSH sandbox (via explorer.exe) as the normal user,
rem because the sandbox blocks the named pipe Docker Desktop's API uses.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\24235\Desktop\databaseExperiment\_setup\pull-miniob.ps1"
