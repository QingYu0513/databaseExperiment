@echo off
rem Runs OUTSIDE the DSH sandbox (via explorer.exe) as the normal user.
rem Compiles MiniOB inside the container and runs observer to prove it works.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\24235\Desktop\databaseExperiment\_setup\stage4-verify-container.ps1"
