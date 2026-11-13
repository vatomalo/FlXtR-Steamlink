#!/usr/bin/env bash
# Run as root on SkyLoft after pulling main. No new packages are required.
set -euo pipefail
[[ $(id -u) == 0 ]] || { echo 'Run this installer as root.' >&2; exit 1; }
repo=$(cd "$(dirname "$0")/.." && pwd)
storage=/opt/dockers/storage
account=namikazeruisu
getent passwd "$account" >/dev/null
test -d /etc/apache2
mountpoint -q "$storage"
backup="$storage/build-portal/backups/$(date -u +%Y%m%dT%H%M%SZ)"
install -d -m 0700 -o "$account" -g "$account" "$storage/build-portal" "$storage/build-portal/jobs"
install -d -m 0700 "$backup"
if [[ -f /var/www/html/index.html ]]; then cp -p /var/www/html/index.html "$backup/index.html"; fi
if [[ -f /etc/apache2/conf-available/flxtr-build.conf ]]; then cp -p /etc/apache2/conf-available/flxtr-build.conf "$backup/"; fi
install -d -m 0755 /var/www/html "$storage/tools/libexec/flxtr-portal" "$storage/tools/libexec/flxtr-build"
install -m 0644 "$repo/web/skyloft/index.html" /var/www/html/index.html
install -m 0755 "$repo/tools/build_portal.py" "$storage/tools/libexec/flxtr-portal/build_portal.py"
install -m 0755 "$repo/scripts/flxtr-build" /usr/local/bin/flxtr-build
install -m 0644 "$repo/scripts/verify-android-abis.py" "$storage/tools/libexec/flxtr-build/verify-android-abis.py"
install -m 0755 "$repo/scripts/publish-android.py" "$storage/tools/libexec/flxtr-build/publish-android.py"
for runner in build-skyloft-android.sh build-skyloft-steamlink.sh build-skyloft-desktop.sh publish-skyloft.sh; do
    install -m 0755 -o "$account" -g "$account" "$repo/scripts/$runner" "$storage/tools/libexec/flxtr-build/$runner"
done
chown "$account:$account" "$storage/tools/libexec/flxtr-build"
cat > /etc/apache2/conf-available/flxtr-build.conf <<'APACHE'
# This console can execute builds. Expose it only through the tailnet or loopback.
<Directory "/var/www/html">
    Require ip 100.64.0.0/10 fd7a:115c:a1e0::/48 127.0.0.1 ::1
</Directory>
<Location "/api/">
    Require ip 100.64.0.0/10 fd7a:115c:a1e0::/48 127.0.0.1 ::1
</Location>
ProxyPreserveHost On
ProxyPass "/api/" "http://127.0.0.1:8787/api/" connectiontimeout=5 timeout=60
ProxyPassReverse "/api/" "http://127.0.0.1:8787/api/"
APACHE
cat > /etc/systemd/system/flxtr-build-portal.service <<'SERVICE'
[Unit]
Description=FlXtR private build console API
After=network-online.target docker.service tailscaled.service
Wants=network-online.target

[Service]
Type=simple
User=namikazeruisu
Group=namikazeruisu
SupplementaryGroups=docker
WorkingDirectory=/opt/dockers/storage/tools/libexec/flxtr-portal
Environment=HOME=/home/namikazeruisu
Environment=PYTHONUNBUFFERED=1
Environment=FLXTR_PORTAL_HOSTS=127.0.0.1,localhost,100.69.7.81,skyloft,skyloft.tailac13cd.ts.net
ExecStart=/usr/bin/python3 /opt/dockers/storage/tools/libexec/flxtr-portal/build_portal.py
Restart=on-failure
RestartSec=2
# Workers persist across a web API restart and write their own final status.
KillMode=process
UMask=0077

[Install]
WantedBy=multi-user.target
SERVICE
a2enmod proxy proxy_http
a2enconf flxtr-build
apache2ctl configtest
systemctl daemon-reload
systemctl enable --now flxtr-build-portal.service
systemctl restart flxtr-build-portal.service
systemctl reload apache2
echo 'FlXtR build console: http://100.69.7.81/ (Tailscale only)'
echo "Previous web page saved in $backup"
