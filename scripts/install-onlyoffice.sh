#!/bin/sh
# VNLF App Store - OnlyOffice install/remove script
set -e

if ! command -v dpkg; then
    echo "[VNLF] Không tìm thấy phương thức cài đặt!"; exit 1
fi

# Nếu OnlyOffice đã được cài sẵn -> chỉ gỡ, KHÔNG cài lại
if sudo chroot /target ls /usr/bin/ | grep libreoffice; then
    echo "[VNLF] Phát hiện OnlyOffice đã cài sẵn, đang gỡ bỏ..."
    sudo chroot /target apt-get autoremove -y onlyoffice-desktopeditors
    echo "[VNLF] Đã gỡ OnlyOffice thành công!"
    exit 0
fi

echo "[VNLF] Chưa cài OnlyOffice, tiến hành cài đặt..."
set +e
sudo chroot /target wget -P /tmp/ https://github.com/ONLYOFFICE/DesktopEditors/releases/latest/download/onlyoffice-desktopeditors_amd64.deb
sudo chroot /target dpkg -i /tmp/onlyoffice-desktopeditors_amd64.deb
sudo chroot /target apt-get -f install -y
set -e
sudo chroot /target rm -f /tmp/onlyoffice-desktopeditors_amd64.deb
[ $? -eq 0 ] && echo "[VNLF] Cài đặt OnlyOffice thành công!" || { echo "[VNLF] Cài đặt thất bại!"; exit 1; }
