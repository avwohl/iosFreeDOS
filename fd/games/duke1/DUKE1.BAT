@echo off
REM DOS Games Archive DOSBox launch script
REM 
REM Game: Duke Nukem Shareware 2.0
REM Configuration: standard
REM 
config -set cpu "cycles=auto 6075 98%% limit 23880"
config -set render aspect=true
loadfix -25
cd DUKE
DN1.EXE
loadfix -f
cd..
cls
echo Thank you for playing Duke Nukem Shareware!
echo.
echo To play again, type DUKE1 and press Enter
echo.
echo Your DOS Games Archive http://www.dosgamesarchive.com/
@echo on