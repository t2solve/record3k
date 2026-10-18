# How to mount a network share (SMB/CIFS) on Linux

For use as the `--remote` / `REMOTE` destination in [tools/archive_records.py](../../tools/archive_records.py) and [tools/run.sh](../../tools/run.sh).

Assumes a Windows-style network share (`\\server\share`, accessed with a username + password), i.e. SMB/CIFS. If your share is actually NFS, mounting doesn't use credentials this way - ask and a separate guide can be written.

**If you don't have root/sudo**, skip to [Mounting without root](#mounting-without-root-into-your-home-folder) below - the mountpoint can be anywhere including your home folder either way, but the plain `mount -t cifs` approach in sections 1-5 always needs root for the actual mount syscall.

## 1. Install the CIFS client

```bash
sudo apt-get update
sudo apt-get install -y cifs-utils
```

## 2. Create a credentials file (don't put the password on the command line or in /etc/fstab directly)

```bash
sudo mkdir -p /etc/samba/creds
sudo tee /etc/samba/creds/record3k_archive > /dev/null <<'EOF'
username=YOUR_USERNAME
password=YOUR_PASSWORD
domain=YOUR_DOMAIN_IF_ANY
EOF
sudo chmod 600 /etc/samba/creds/record3k_archive
```

Leave out the `domain=` line if the share doesn't need one.

## 3. Create the local mount point

```bash
sudo mkdir -p /mnt/record3k_archive
```

## 4. One-off test mount

Replace `SERVER` and `SHARE` with your actual host/share name:

```bash
sudo mount -t cifs //SERVER/SHARE /mnt/record3k_archive \
  -o credentials=/etc/samba/creds/record3k_archive,vers=3.0,uid=$(id -u),gid=$(id -g),file_mode=0664,dir_mode=0775
```

Notes on the options:
- `vers=3.0` pins the SMB protocol version. Newer/stable versions (3.0/3.1.1) are less prone to the
  session drops that show up as `Software caused connection abort (103)` mid-transfer - which is exactly
  the error we saw earlier from rsync writing to a flaky mount. Avoid `vers=1.0`.
- `uid=$(id -u),gid=$(id -g)` makes files appear owned by your own Linux user, since SMB has no native
  concept of Linux uid/gid.

Verify it's mounted:

```bash
mount | grep record3k_archive
df -h /mnt/record3k_archive
```

Unmount when done testing:

```bash
sudo umount /mnt/record3k_archive
```

## 5. Make it persistent (mount at boot / on demand)

Add a line to `/etc/fstab`:

```bash
sudo tee -a /etc/fstab > /dev/null <<'EOF'
//SERVER/SHARE  /mnt/record3k_archive  cifs  credentials=/etc/samba/creds/record3k_archive,vers=3.0,uid=1000,gid=1000,file_mode=0664,dir_mode=0775,_netdev,x-systemd.automount,x-systemd.mount-timeout=10  0  0
EOF
```

Replace `uid=1000,gid=1000` with your actual `id -u`/`id -g` (fstab can't run `$(id -u)` inline).

- `_netdev` tells the boot process this mount depends on the network being up first.
- `x-systemd.automount` mounts it lazily on first access instead of blocking boot if the share is
  slow/unavailable.

Apply without rebooting:

```bash
sudo systemctl daemon-reload
sudo mount -a
```

## 6. Point the archive tool at it

Update `REMOTE=` in [tools/run.sh](../../tools/run.sh) to `/mnt/record3k_archive` (or a subdirectory under it).

## Mounting without root (into your home folder)

`mount -t cifs` always needs root for the actual mount syscall, no matter where the mountpoint is -
it's not a "home folder vs /mnt" restriction. If you don't have sudo at all, use one of these instead.

### Option A: rclone mount (recommended - works headless, no root)

rclone's SMB backend mounts via FUSE in userspace. Works well for an unattended `tools/run.sh` /
cron setup since it needs no desktop session.

```bash
# install (one of):
sudo apt-get install -y rclone   # if someone with root can run this once for you
# or, fully as your own user, no root needed at all:
curl -O https://downloads.rclone.org/rclone-current-linux-amd64.zip
unzip rclone-current-linux-amd64.zip && mkdir -p ~/bin && cp rclone-*/rclone ~/bin/ && export PATH="$HOME/bin:$PATH"

# configure the share (interactive wizard - choose type "smb")
rclone config
#   name> record3k_archive
#   type> smb
#   host> SERVER
#   user> YOUR_USERNAME
#   pass> (enter interactively, it gets obscured/stored in ~/.config/rclone/rclone.conf)

# mount it into your home folder
mkdir -p ~/mnt/record3k_archive
rclone mount record3k_archive:SHARE ~/mnt/record3k_archive --daemon --vfs-cache-mode writes

#real
rclone mount record3k_archive:camflow01 ~/mnt/record3k_archive --daemon  --vfs-cache-mode writes
```

Point `REMOTE=` in `tools/run.sh` at `~/mnt/record3k_archive` (expanded to an absolute path). To
unmount: `fusermount -u ~/mnt/record3k_archive`.

This needs `/dev/fuse` to be usable by your user, which is the default on most distros (Debian/Ubuntu:
being in the `fuse` group or just having `user_allow_other` unset is fine for your own mounts).

### Option B: gio/gvfs mount (desktop sessions only)

If you're on a machine with a GNOME/desktop session (D-Bus running for your user), no extra install
is usually needed:

```bash
gio mount smb://SERVER/SHARE
```

It'll prompt for username/password and mount under `/run/user/$(id -u)/gvfs/smb-share:server=SERVER,share=SHARE/`.
Symlink that into your home folder for a friendlier path:

```bash
ln -s "/run/user/$(id -u)/gvfs/smb-share:server=SERVER,share=SHARE" ~/mnt/record3k_archive
```

Not ideal for a headless cron job - the gvfs mount only exists while your user session is active.

### Option C: ask an admin for one `/etc/fstab` line, then mount yourself afterward

If someone with root is willing to add the entry once, include the `user` (or `users`) option:

```
//SERVER/SHARE  /home/youruser/mnt/record3k_archive  cifs  credentials=/home/youruser/.smbcreds,vers=3.0,uid=youruser,gid=youruser,user,noauto  0  0
```

After that one-time setup, you can mount/unmount it yourself without sudo:

```bash
mount /home/youruser/mnt/record3k_archive
umount /home/youruser/mnt/record3k_archive
```

(`user`/`noauto` together mean: not mounted automatically at boot, but any user - specifically whoever
mounted it - may mount/unmount that exact line without root.)

## Troubleshooting

- `mount error(13): Permission denied` - wrong username/password/domain in the credentials file.
- `mount error(112): Host is down` / timeouts - check the server is reachable (`ping SERVER`), and that
  SMB (port 445) isn't blocked by a firewall.
- Random disconnects during large transfers (the `connection abort (103)` error) - check
  `dmesg | grep -i cifs` for session reset messages around the failure time, and consider adding
  `echo_interval=30` (or similar keepalive tuning) to the mount options; `tools/archive_records.py`
  already passes `--partial` to rsync so an interrupted transfer resumes instead of restarting.
