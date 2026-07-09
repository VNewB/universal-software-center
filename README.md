# VNLF App Store

Kho ứng dụng Linux tiếng Việt — ứng dụng desktop viết bằng **C++ + GTK3**.

Giao diện lấy cảm hứng từ [VNLF App Explorer](https://apps.vietnamlinuxfamily.net/), nhưng chạy hoàn toàn trên desktop Linux.

---

## Tính năng

- 🔍 **Tìm kiếm** ứng dụng theo tên, mô tả, thẻ
- 📂 **Lọc theo danh mục**: Internet, Đa phương tiện, Văn phòng, Đồ họa, Lập trình, Hệ thống...
- 📋 **Xem chi tiết** ứng dụng: mô tả, tác giả, phiên bản, giấy phép, website
- ⚙️ **Cài đặt** bằng nút "Cài đặt" — gọi script bash có sẵn
- 🖥️ **Cửa sổ terminal inline** hiển thị tiến trình cài đặt thời gian thực
- 📦 **Dữ liệu ứng dụng** từ file `data/apps/apps.json` — dễ mở rộng

---

## Cấu trúc dự án

```
vnlf-store/
├── src/
│   └── main.cpp          # Toàn bộ source code C++/GTK3
├── data/
│   ├── apps
│   |   ├── files.json
│   |   └── ... 
│   ├── icons
│   |   ├── icons.png
│   │   └── ....png   # Danh sách ứng dụng (dễ chỉnh sửa)
├── scripts/
│   ├── install-generic.sh      # Script cài đặt tổng quát
│   ├── install-firefox.sh
│   ├── install-vlc.sh
│   ├── install-libreoffice.sh
│   ├── install-gimp.sh
│   ├── install-vscode.sh
│   ├── install-obs.sh
│   └── ... (1 script/app)
├── Makefile
└── README.md
```

---

## Yêu cầu

```bash
# Ubuntu/Debian
sudo apt install libgtk-3-dev pkg-config g++ make libvte-2.91-dev

# Fedora/RHEL
sudo dnf install gtk3-devel gcc-c++ make pkgconf vte291-devel

# Arch Linux
sudo pacman -S gtk3 gcc make pkgconf vte3
```

---

## Build & Chạy

```bash
# Build
make

# Chạy trực tiếp
./store

# Hoặc build + chạy luôn
make run

# Cài đặt vào hệ thống
sudo make install
```

---

## Thêm ứng dụng mới

### 1. Thêm vào `data/apps.json`

```json
{
  "id": "myapp",
  "name": "Tên ứng dụng",
  "summary": "Mô tả ngắn",
  "description": "Mô tả đầy đủ về ứng dụng...",
  "category": "internet",
  "icon": "tên-icon-gtk",
  "version": "1.0.0",
  "author": "Tác giả",
  "license": "GPL-3.0",
  "website": "https://...",
  "script": "install-myapp.sh",
  "tags": ["thẻ1", "thẻ2"]
}
```

**Danh mục hợp lệ:** `internet`, `multimedia`, `office`, `graphics`, `development`, `system`, `games`, `education`, `utilities`

**Tên icon:** Dùng tên icon GTK chuẩn (vd: `firefox`, `vlc`, `gimp`, `code`...). Xem danh sách tại https://specifications.freedesktop.org/icon-naming-spec/latest/

### 2. (Không bắt buộc) Tạo script cài đặt `scripts/install-myapp.sh`

```bash
#!/bin/bash
echo "[VNLF] Cài đặt Tên App..."

if command -v apt &>/dev/null; then
    sudo apt install -y myapp-package
elif command -v dnf &>/dev/null; then
    sudo dnf install -y myapp-package
elif command -v pacman &>/dev/null; then
    sudo pacman -S --noconfirm myapp-package
else
    echo "[VNLF] Không tìm thấy package manager!"
    exit 1
fi

[ $? -eq 0 ] && echo "[VNLF] Cài đặt thành công!" || exit 1
```

```bash
chmod +x scripts/install-myapp.sh
```

---

## Cơ chế cài đặt (hiện tại dù chưa ngon, còn chậm và tắc nghẽn nhưng tương lai cơ chế sẽ ngon hơn rất nhiều)

Khi người dùng nhấn **Cài đặt**:
0. init(lần khởi động đầu tiên, khi chưa có file installed.json): phần mềm sẽ đọc các "id" ứng dụng trong các file .json đồng thời thực hiện tìm kiếm các package đã cài đặt trong hệ thống.
1. Phần mềm đọc "id" ứng dụng trong các file json đồng thời đọc mục "script" -> nếu có thì thêm phương pháp cài đặt là script.
2. Check không có script thì mặc định sẽ sử dụng các package manager có sẵn + install "id".
3. Flatpak user thì không cần sudo - các package manager của hệ thống và flatpak system thì có -> nhập mật khẩu sudo.
4. Cài đặt xong sẽ cập nhật file log tại ~/.local/share/vnlf-store/installed.json (chắc chắn trong tương lai sẽ là ở trong thư mục /var/, hiện tại chạy ở cấp độ user nên lưu ở ~/ là ổn).

Khi người dúng nhấn **Gỡ cài đặt**:
1. Đọc thông tin ứng dụng và package manager trong installed.json.
2. Nhập mật khẩu sudo nếu cần.
3. Tiến hành gỡ cài đặt theo package manager.
4. Nếu phần mềm cài bằng script, dev cần viết cơ chế uninstall trong file install-appnames.sh
5. Cập nhật lại file installed.json.

---

## Tùy chỉnh giao diện

Toàn bộ CSS nằm trong hằng số `APP_CSS` trong `src/main.cpp`. Chỉnh màu sắc, font, bo góc... tại đó.

---

---
## Những điều cụ thể cần cải thiện
- Thêm nút update, upgrade (nếu cần).
- Thêm nút scan lại ứng dụng khi gặp lỗi thông tin với file installed.json.
- Cơ chế tìm kiếm và tự động cập nhật thông tin phần mềm, database local theo thời gian.
- Bổ sung các thuật toán tìm kiếm hoặc preload hoặc ... để tránh tắc nghẽn, mở app center chậm khi số lượng phần mềm, ứng quá nhiều so với thời điểm hiện tại.
- Sử dụng tài nguyên online (hiện tại phần mềm hoạt động offline gần như hoàn toàn).
- Thay đổi cách phần mềm hoạt động để trở nên linh hoạt hơn (dynamic), hiện tại vì sử dụng tài nguyên offline và khá static.
... Còn nữa, chủ yếu tìm thấy trong issues.
---

*VNLF App Store — Cộng đồng Linux Việt Nam 🇻🇳*
