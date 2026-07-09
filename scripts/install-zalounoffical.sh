#!/bin/sh
echo "Cài Zalo..."
set -e
APP_NAME="Zalo"
APPIMAGE_URL="https://github.com/realdtn2/zalo-linux-2026/releases/download/v1.1.3/Zalo-v1.1.3-x86_64.AppImage"
INSTALL_DIR="/usr/local/bin"
APPIMAGE_PATH="$INSTALL_DIR/Zalo.AppImage"
DESKTOP_FILE="/usr/share/applications/zalo.desktop"
SKEL_DESKTOP_DIR="/etc/skel/Desktop"
SKEL_DESKTOP_FILE="$SKEL_DESKTOP_DIR/zalo.desktop"
ICON_PATH="/usr/share/pixmaps/zalo.png"

# Nếu Zalo đã được cài sẵn -> chỉ gỡ, KHÔNG cài lại
if [ -f "$APPIMAGE_PATH" ]; then
    echo "==> Phát hiện Zalo đã cài sẵn, đang gỡ bỏ..."
    sudo chroot /target rm -f "$APPIMAGE_PATH"
    sudo chroot /target rm -f "$DESKTOP_FILE"
    sudo chroot /target rm -f "$SKEL_DESKTOP_FILE"
    sudo chroot /target rm -f "$ICON_PATH"
    sudo chroot /target update-desktop-database /usr/share/applications
    echo "==> Đã gỡ Zalo thành công!"
    exit 0
fi

echo "==> Chưa cài Zalo, tiến hành cài đặt..."

echo "==> Tạo thư mục"
sudo chroot /target mkdir -p "$INSTALL_DIR" "$SKEL_DESKTOP_DIR"

echo "==> Tải AppImage"
sudo chroot /target wget -O "$APPIMAGE_PATH" "$APPIMAGE_URL"

echo "==> Cấp quyền thực thi"
sudo chroot /target chmod +x "$APPIMAGE_PATH"

echo "==> Tải icon"
sudo chroot /target wget -O "$ICON_PATH" "https://upload.wikimedia.org/wikipedia/commons/9/91/Icon_of_Zalo.svg"

echo "==> Tạo shortcut .desktop"
sudo chroot /target cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Name=Zalo
Comment=Good for the old and the rich, not for me!
Exec=$APPIMAGE_PATH
Icon=$ICON_PATH
Terminal=false
Type=Application
Categories=Network;Chat;
StartupNotify=true
EOF
sudo chroot /target chmod +x "$DESKTOP_FILE"

echo "==> Đưa Zalo ra Desktop cho user mặc định"
sudo chroot /target cp "$DESKTOP_FILE" "$SKEL_DESKTOP_FILE"
sudo chroot /target chmod +x "$SKEL_DESKTOP_FILE"
sudo chroot /target gio set "$SKEL_DESKTOP_FILE" metadata::trusted true
# Cinnamon/Nemo nhận shortcut desktop là trusted nếu có attribute này.
# Nếu gio metadata không khả dụng trong chroot, chmod +x vẫn đảm bảo có thể mở shortcut.

echo "==> Cập nhật database ứng dụng"
sudo chroot /target update-desktop-database /usr/share/applications

echo ""
echo "Cài đặt hoàn tất."
echo "Mở Zalo từ Desktop hoặc menu ứng dụng."
