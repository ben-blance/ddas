@echo off 
echo ======================================== 
echo   Stopping DDAS 
echo ======================================== 
echo. 
echo Stopping GUI application... 
taskkill /F /IM ddas_gui.exe 2>nul 
echo. 
echo Stopping detection engine... 
taskkill /F /IM ddas_engine.exe 2>nul 
echo. 
echo ======================================== 
echo DDAS has been stopped 
echo ======================================== 
pause 
