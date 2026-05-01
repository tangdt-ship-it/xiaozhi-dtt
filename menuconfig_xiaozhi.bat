@echo off
setlocal
cd /d "%~dp0upstream_xiaozhi"
set "DTT_IDF_PYTHON_ENV=C:\Espressif\python_env\idf5.5_py3.14_env"
set "IDF_PYTHON_ENV_PATH=%DTT_IDF_PYTHON_ENV%"
set "PYTHON=%DTT_IDF_PYTHON_ENV%\Scripts\python.exe"
set "PATH=%DTT_IDF_PYTHON_ENV%\Scripts;%DTT_IDF_PYTHON_ENV%;%PATH%"
call "%USERPROFILE%\.platformio\packages\framework-espidf\export.bat"
"%DTT_IDF_PYTHON_ENV%\Scripts\python.exe" "%USERPROFILE%\.platformio\packages\framework-espidf\tools\idf.py" menuconfig
endlocal
