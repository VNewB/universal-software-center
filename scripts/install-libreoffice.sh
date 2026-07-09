#!/bin/sh
# install-libreoffice.sh - VNLF App Store
set -e

if ! command -v apt; then
    echo "[VNLF] Không tìm thấy package manager!"; exit 1
fi

# Nếu LibreOffice đã được cài sẵn -> chỉ gỡ, KHÔNG cài lại
if sudo chroot /target ls /usr/bin/ | grep libreoffice; then
    echo "[VNLF] Phát hiện LibreOffice đã cài sẵn, đang gỡ bỏ..."
    sudo chroot /target apt-get autoremove -y libreoffice
    echo "[VNLF] Đã gỡ LibreOffice thành công!"
    exit 0
fi

echo "[VNLF] Chưa cài LibreOffice, tiến hành cài đặt..."
set +e
sudo chroot /target apt update
sudo chroot /target apt install -y libreoffice
set -e

[ $? -eq 0 ] && echo "[VNLF] Cài đặt LibreOffice thành công!" || { echo "[VNLF] Cài đặt thất bại!"; exit 1; }
