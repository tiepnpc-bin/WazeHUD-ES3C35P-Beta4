# Build WazeHUD ES3C35P trên GitHub Actions

1. Tạo repository GitHub mới, tải **toàn bộ nội dung thư mục `source/`** lên gốc repository (không tải nguyên ZIP lên như một file).
2. Vào **Actions** → **WazeHUD ES3C35P Firmware** → **Run workflow**.
3. Nếu build thành công, tải artifact `WazeHUD-ES3C35P-firmware` chứa các `.bin` và `flasher_args.json` (địa chỉ nạp).
4. Nếu build thất bại, tải `WazeHUD-build-log` và gửi log để sửa. Không nạp `.bin` cũ từ Beta1 dưới tên Beta4.
5. Chỉ nạp khi đã đối chiếu đúng bo ES3C35P và các địa chỉ từ `flasher_args.json`. Sao lưu dữ liệu cấu hình nếu cần.

**Trạng thái:** Mã nguồn đã qua host tests. Chưa chạy GitHub Actions, chưa xác nhận compile, chưa kiểm tra bo thật.

**Lưu ý submodule:** Nếu thư mục nguồn được tải lên GitHub bằng web upload thay vì git clone, các git submodule có thể thiếu. Khi đó cần đồng bộ đúng dependencies theo mã nguồn trước khi build.
