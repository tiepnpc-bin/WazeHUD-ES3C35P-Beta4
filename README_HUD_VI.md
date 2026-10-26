# Bản tùy biến WazeHUD + Xiaozhi cho ES3C35P — Beta 4

Mục tiêu riêng: ESP32-S3 N16R8, ST77922 QSPI, ES8311, màn ngang 480×320. Giữ điều kiện `>=`. Bản này chưa được thử trên phần cứng thật.

## Build

Dùng ESP-IDF **6.0.1** và các submodule đúng phiên bản. Mở terminal ESP-IDF trong thư mục source:

```
idf.py -D IDF_TARGET=esp32s3 -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.hud.defaults" build
```

Hoặc PowerShell: `./BUILD_HUD.ps1`. Profile đã được kiểm tra: LCDWiki ES3C35P, tiếng Việt, NimBLE, hotspot provisioning, wake word disabled. Không bật BluFi cùng NimBLE. `dependencies.lock` ghim thư viện đã dùng để build.

## Kiến trúc

- `main/hud/bluetooth`: BLE HLP/1 từ WazeHUD.
- `main/hud/protocol`: framing, UTF-8, handshake, JSON và chống trạng thái cũ.
- `main/hud/state`: snapshot qua FreeRTOS queue, điều kiện cảnh báo dùng chung.
- `main/hud/config`: cấu hình HLP/NVS bốn trường được hỗ trợ ở bản ghép.
- `main/hud/hud_integration.cc`: LVGL HUD và điều phối âm thanh, không tạo driver LCD/I2S thứ hai.
- `main/hud/audio`: chính sách cảnh báo và giọng nói Opus.
- `main/application.cc`: ngắt hội thoại/đóng kênh audio, bảo vệ ưu tiên HUD và chặn auto-upgrade bản gốc.
- `main/boards/lcdwiki-es3c35p`: driver màn/micro/loa/cảm ứng của Xiaozhi; xoay LVGL bằng phần mềm, căn vùng 4 pixel và đổi tọa độ cảm ứng.
- `main/audio/codecs/es8311_audio_codec.cc`: tắt PA khi ngừng phát, không tắt clock duplex.

## Kiểm tra host

Linux có g++: `python3 tests/run_host_tests.py`.

Kiểm tra ngưỡng dưới/bằng/trên, offset, dữ liệu cũ, giới hạn không rõ, nhịp cảnh báo, BLE fragmentation/UTF-8, hồi phục sau frame quá dài, giữ khoảng cách và thứ tự timestamp. Có AddressSanitizer và UndefinedBehaviorSanitizer. LeakSanitizer tắt vì môi trường build không cung cấp /proc tương ứng.

Các kiểm tra này không thay thế thử màn hình, cảm ứng, micro/loa hoặc Wi-Fi + BLE trên bo thật. GUI Beta dùng chữ cho nhiều loại biển; làn hiển thị trạng thái chọn và direction mask HLP, ETA và rẽ thứ hai đã có vùng hiển thị riêng.

## Giấy phép và nguồn

Xem `HUD_SOURCES.md`, `LICENSE.HUD.GPL-3.0` và các thông báo MIT gốc. Mã kết hợp chứa GPL-3.0; khi phân phối bản sửa cần cung cấp mã nguồn tương ứng và giữ thông báo các tác giả. Không dùng README upstream dành cho T-Display làm hướng dẫn nạp bản này.
