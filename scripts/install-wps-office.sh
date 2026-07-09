#!/bin/sh
# VNLF App Store - WPS Office CN install/remove script
set -e
export DEBIAN_FRONTEND=noninteractive

WPS_VER="12.1.2.25882"

_get_wps_url() {
    local arch="$1"
    local furl="https://wps-linux-personal.wpscdn.cn/wps/download/ep/Linux2023/${WPS_VER##*.}/wps-office_${WPS_VER}.AK.preread.sw.Personal_662820_${arch}.deb"
    local uri="${furl#https://wps-linux-personal.wpscdn.cn}"
    local secrityKey='7f8faaaa468174dc1c9cd62e5f218a5b'
    local timestamp10=$(date '+%s')
    local md5hash=$(echo -n "${secrityKey}${uri}${timestamp10}" | md5sum)
    echo "${furl}?t=${timestamp10}&k=${md5hash%% *}"
}

if ! command -v dpkg; then
    echo "[VNLF] Không tìm thấy phương thức cài đặt!"; exit 1
fi

# Nếu WPS đã được cài sẵn -> chỉ gỡ, KHÔNG cài lại
if sudo chroot /target ls /usr/bin/ | grep wps; then
    echo "[VNLF] Phát hiện WPS Office đã cài sẵn, đang gỡ bỏ..."
    sudo chroot /target apt-get autoremove -y wps-office
    echo "[VNLF] Đã gỡ WPS Office thành công!"
    exit 0
fi

echo "[VNLF] Chưa cài WPS Office, tiến hành cài đặt..."

echo "[VNLF] Đang tải WPS Office ${WPS_VER}..."
WPS_URL=$(_get_wps_url amd64)
sudo wget -q -O /tmp/wps-office.deb "$WPS_URL" || true

if [ ! -s /tmp/wps-office.deb ] || ! file /tmp/wps-office.deb | grep -qi "Debian binary package"; then
    echo "[VNLF] Không tải được WPS Office!" >&2
    rm -f /tmp/wps-office.deb
    exit 1
fi

echo "[VNLF] Đang giải nén gói .deb (định dạng ar)..."
cd /tmp
ar x wps-office.deb
tar -xf data.tar.xz

echo "[VNLF] Đang xoá gói ngôn ngữ zh_CN..."
rm -rf /tmp/opt/kingsoft/wps-office/office6/mui/zh_CN

echo "[VNLF] Đang xoá wtool (cơ chế cập nhật)..."
rm -rf /tmp/opt/kingsoft/wps-office/office6/wtool

echo "[VNLF] Đang đóng gói lại WPS Office..."
tar -cJf data.tar.xz ./opt/ ./etc/ ./usr/
ar rcs wps-office-modified.deb debian-binary control.tar.gz data.tar.xz

echo "[VNLF] Đang chép gói cài đặt vào /target..."
sudo mkdir -p /target/tmp
sudo cp /tmp/wps-office-modified.deb /target/tmp/wps-office_${WPS_VER}_amd64.deb

echo "[VNLF] Đang cài đặt WPS Office vào /target..."
set +e
sudo chroot /target dpkg -i /tmp/wps-office_${WPS_VER}_amd64.deb
sudo chroot /target apt-get -f install -y
set -e

echo "[VNLF] Đang dọn dẹp..."
rm -rf /tmp/wps-office.deb /tmp/wps-office-modified.deb /tmp/data.tar.xz /tmp/control.tar.gz /tmp/debian-binary /tmp/opt /tmp/etc /tmp/usr
sudo rm -f /target/tmp/wps-office_${WPS_VER}_amd64.deb

echo "[VNLF] Cài đặt WPS Office thành công!"
