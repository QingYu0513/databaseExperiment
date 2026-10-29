@echo off
rem Runs OUTSIDE the DSH sandbox (via explorer.exe). Builds the local MiniOB dev image
rem from the already-present ubuntu:24.04 base (Docker Hub / github-in-container are unreachable).
set LOG=C:\Users\24235\Desktop\databaseExperiment\_setup\build_image.log
set DOCKER="C:\Program Files\Docker\Docker\resources\bin\docker.exe"
set CTX=C:\Users\24235\Desktop\databaseExperiment\_setup\buildctx
if not exist "%CTX%" mkdir "%CTX%"
echo ==== build start %DATE% %TIME% ==== > "%LOG%"
echo ---- base image present? ---- >> "%LOG%"
%DOCKER% images ubuntu:24.04 >> "%LOG%" 2>&1
echo ---- building oceanbase/miniob:latest from Dockerfile.dev ---- >> "%LOG%"
%DOCKER% build --progress=plain -t oceanbase/miniob:latest -f "C:\Users\24235\Desktop\databaseExperiment\_setup\Dockerfile.dev" "%CTX%" >> "%LOG%" 2>&1
echo build exit=%ERRORLEVEL% >> "%LOG%"
echo ---- images ---- >> "%LOG%"
%DOCKER% images >> "%LOG%" 2>&1
echo ==== build done %DATE% %TIME% ==== >> "%LOG%"
