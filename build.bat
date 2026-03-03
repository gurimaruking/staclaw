@echo off
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5.2-2
set IDF_PYTHON_ENV_PATH=C:\Espressif\python_env\idf5.5_py3.11_env
set IDF_TOOLS_PATH=C:\Espressif
set PATH=C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin;C:\Espressif\tools\xtensa-esp-elf-gdb\16.3_20250913\xtensa-esp-elf-gdb\bin;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\python_env\idf5.5_py3.11_env\Scripts;C:\Espressif\tools\idf-git\2.44.0\cmd;%PATH%

cd /d E:\OneDrive\claudecode\m5stack\staclaw
C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe %IDF_PATH%\tools\idf.py %*
