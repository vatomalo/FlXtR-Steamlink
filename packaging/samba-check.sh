#!/bin/sh
# Check whether firmware includes Samba server support.
for tool in smbd smbpasswd smbcontrol; do
 if command -v "$tool" >/dev/null 2>&1; then echo "$tool: available"; else echo "$tool: missing"; fi
done
ls -ld /home/apps/retroarch/roms /home/apps/greenlink/videos 2>/dev/null || true
