# DTT Xiaozhi Build And Flash

Firmware Xiaozhi day du nam trong thu muc `upstream_xiaozhi`.

## Cau hinh da co dinh

- Board: ESP32-S3 N16R8
- Flash: 16 MB
- PSRAM: 8 MB Octal PSRAM
- Ngon ngu: tieng Viet (`vi-VN`)
- Wake word: `He lo`
- Wake word model: English MultiNet 7 (`mn7_en`), duration 7000 ms
- Wake word threshold: 20
- WakeNet9s multiple wake words: tat tat ca
- OLED: SSD1306 128x32, SDA GPIO47, SCL GPIO21
- Micro INMP441: SD GPIO42, WS GPIO1, SCK GPIO2, left channel
- Speaker MAX98357A: DIN GPIO39, BCLK GPIO40, LRC GPIO41, left channel
- LED trang thai: GPIO8
- BOOT button: GPIO0
- PS2 receiver: CLK GPIO14, CMD GPIO13, ATT GPIO12, DAT GPIO11

## Build

Mo PowerShell tai thu muc project:

```powershell
cd C:\Ai-dtt\dtt-esp32\upstream_xiaozhi
powershell -ExecutionPolicy Bypass -NoProfile -Command "& '$env:USERPROFILE\.platformio\packages\framework-espidf\export.ps1'; & '$env:USERPROFILE\.platformio\packages\framework-espidf\tools\idf.py' build"
```

Build da duoc kiem tra thanh cong. File firmware chinh:

```text
C:\Ai-dtt\dtt-esp32\upstream_xiaozhi\build\xiaozhi.bin
```

## Menuconfig

De dung dung lenh `idf.py menuconfig`, mo PowerShell trong firmware Xiaozhi va export ESP-IDF truoc:

```powershell
cd C:\Ai-dtt\dtt-esp32\upstream_xiaozhi
& "$env:USERPROFILE\.platformio\packages\framework-espidf\export.ps1"
idf.py menuconfig
```

Trong menu `Esp Speech Recognition`:

- `Load Multiple Wake Words (WakeNet9s)`: bo chon tat ca.
- `English Speech Commands Model`: chon `general english recognition (mn7_en)`.

Neu khong muon export thu cong moi lan, co the dung script kem san:

```powershell
cd C:\Ai-dtt\dtt-esp32\upstream_xiaozhi
.\idf.ps1 menuconfig
```

Neu PowerShell chan file `.ps1`, dung file `.bat`:

```cmd
cd /d C:\Ai-dtt\dtt-esp32\upstream_xiaozhi
menuconfig.bat
```

Hoac chay tu thu muc goc project:

```cmd
cd /d C:\Ai-dtt\dtt-esp32
menuconfig_xiaozhi.bat
```

Neu dang dung PowerShell trong VS Code, bat buoc them `.\` truoc ten file:

```powershell
cd C:\Ai-dtt\dtt-esp32
.\menuconfig_xiaozhi.bat
```

File `.bat` da duoc co dinh dung Python ESP-IDF:

```text
C:\Espressif\python_env\idf5.5_py3.14_env
```

Neu van gap loi Python environment mismatch, chay mot lan:

```powershell
cd C:\Ai-dtt\dtt-esp32\upstream_xiaozhi
.\idf.bat fullclean
.\menuconfig.bat
```

Neu mo menuconfig bang nut/lenh cua ESP-IDF extension trong VS Code, hay mo workspace dung firmware:

```text
C:\Ai-dtt\dtt-esp32\dtt-xiaozhi.code-workspace
```

Hoac mo truc tiep folder:

```text
C:\Ai-dtt\dtt-esp32\upstream_xiaozhi
```

Neu menuconfig dung, dong dau tien se co menu `Xiaozhi Assistant`.

## Flash

Thay `COMx` bang cong COM cua board:

```powershell
cd C:\Ai-dtt\dtt-esp32\upstream_xiaozhi
chcp 65001
$env:PYTHONIOENCODING="utf-8"
& "$env:USERPROFILE\.platformio\packages\framework-espidf\export.ps1"
& "$env:USERPROFILE\.platformio\packages\framework-espidf\tools\idf.py" -p COMx flash monitor
```

Lenh flash tuong duong ma build da in ra:

```powershell
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0xd000 build\ota_data_initial.bin 0x20000 build\xiaozhi.bin 0x800000 build\generated_assets.bin
```

Neu chi muon mo serial monitor sau khi da nap:

```powershell
cd C:\Ai-dtt\dtt-esp32\upstream_xiaozhi
chcp 65001
$env:PYTHONIOENCODING="utf-8"
& "$env:USERPROFILE\.platformio\packages\framework-espidf\export.ps1"
& "$env:USERPROFILE\.platformio\packages\framework-espidf\tools\idf.py" -p COMx monitor
```

## Lan dau khoi dong

- Nhan giu BOOT de vao che do cau hinh Wi-Fi.
- Sau khi ket noi Wi-Fi, firmware dung dich vu Xiaozhi chinh thuc theo cau hinh mac dinh cua upstream.
- Nhan BOOT ngan de bat/tat trang thai tro chuyen.
