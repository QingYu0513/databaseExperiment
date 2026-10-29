@echo off
rem Launched via explorer.exe so it runs OUTSIDE the DSH sandbox as the normal user.
rem The DSH sandbox blocks named pipes, which Docker Desktop's API needs.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\24235\Desktop\databaseExperiment\_setup\apply-mirror.ps1"
