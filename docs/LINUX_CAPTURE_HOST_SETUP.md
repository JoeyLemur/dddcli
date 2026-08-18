# Linux Capture Host Setup

Complete this one-time setup before using the default capture queue on a Linux host. It covers the operating-system settings that are outside `dddcli`: USB-device access, USB transfer-buffer memory, and the process limits needed to lock capture buffers and request realtime scheduling. The serial-device check also applies when using `auto-capture` or the `player` commands.

The commands that modify `/etc`, the kernel, or systemd require administrator access. The commands below use `sudoedit` for files so the configuration can be reviewed before it is saved.

## Required Values

Run these checks from the same login shell that will run `dddcli`:

```sh
cat /sys/module/usbcore/parameters/usbfs_memory_mb
ulimit -S -l
ulimit -H -l
ulimit -S -r
ulimit -H -r
```

For the default queue, the expected values are:

- `usbfs_memory_mb`: at least `512`
- soft and hard locked memory (`ulimit -S -l` and `ulimit -H -l`): at least `524288` KiB (512 MiB), or `unlimited`
- soft and hard realtime priority (`ulimit -S -r` and `ulimit -H -r`): at least `80`

An untuned host may report `16`, `8192`, and `0`. In particular, a low **hard** `memlock` limit cannot be raised from a normal shell with `ulimit`; it must be changed by the system configuration below.

## 1. Allow Non-Root USB Access

Create a late udev rule:

```sh
sudoedit /etc/udev/rules.d/99-domesday.rules
```

Add this line:

```text
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="603b", MODE="0666"
```

`ATTR{...}` is intentional: `idVendor` and `idProduct` belong to the USB device itself. The `99-` filename makes this rule run after the system default USB rule, which otherwise commonly leaves the device node as `0664 root:root`.

Reload the rules and apply the new rule to an attached device:

```sh
sudo udevadm control --reload-rules
sudo udevadm trigger --action=change --subsystem-match=usb --attr-match=idVendor=1d50 --attr-match=idProduct=603b
```

Verify the device after running `./build/dddcli list-devices`. Use the reported sysfs path with `udevadm`, then map its `BUSNUM` and `DEVNUM` values to `/dev/bus/usb/<bus>/<device>`:

```sh
udevadm info --query=property --path=/sys/bus/usb/devices/4-1
ls -l /dev/bus/usb/004/002
udevadm test /sys/bus/usb/devices/4-1
```

Replace the examples with the values for the connected device. The final device node must be writable by the capture user. In the `udevadm test` output, the default rule should set mode `0664` before `99-domesday.rules` sets `0666`.

## 2. Reserve USBFS Transfer Memory

The default queue uses libusb transfers that need kernel USBFS memory. To set the value until the next reboot:

```sh
echo 512 | sudo tee /sys/module/usbcore/parameters/usbfs_memory_mb
```

To preserve it across reboots, create a modprobe setting:

```sh
sudoedit /etc/modprobe.d/usbcore.conf
```

Add:

```text
options usbcore usbfs_memory_mb=512
```

Some distributions load `usbcore` from the initramfs before the real root filesystem's `/etc/modprobe.d` is available. On Debian/Ubuntu-style systems, rebuild the active initramfs after adding the file:

```sh
sudo update-initramfs -u
```

Reboot, then run `cat /sys/module/usbcore/parameters/usbfs_memory_mb` again. If it is still not `512`, add `usbcore.usbfs_memory_mb=512` to the kernel command line using the bootloader configuration for the host.

## 3. Raise Capture Process Limits

Create a PAM limits file for the user who runs captures:

```sh
sudoedit /etc/security/limits.d/dddcli.conf
```

Replace `captureuser` below with that login name, then add all four lines:

```text
captureuser soft memlock 524288
captureuser hard memlock 524288
captureuser soft rtprio 80
captureuser hard rtprio 80
```

The `memlock` values are KiB. The `soft` and `hard` entries are both required: the hard limit controls the highest value the login shell can use. If a shared capture group is preferable, use `@groupname` in place of `captureuser` on each line; do not use `@` for an individual user.

These limits are inherited when a session starts. Log out of the graphical or SSH session completely and log back in, or reboot. Opening a new terminal tab is not enough if the desktop session was already running. Then verify from the shell that will run the capture:

```sh
ulimit -S -l
ulimit -H -l
ulimit -S -r
ulimit -H -r
```

If either value is still low after a fresh login, first confirm that the host's PAM session stack loads `pam_limits.so`. On systemd desktops, a persistent user manager can also retain the old limits. As a systemd fallback, add a per-user-manager override:

```sh
sudo systemctl edit user@$(id -u).service
```

Add:

```ini
[Service]
LimitMEMLOCK=512M
LimitRTPRIO=80
```

Reboot (or terminate all sessions for that user), log in again, and rerun the two `ulimit` checks.

## 4. Allow Serial-Device Access

This step is needed only for `auto-capture` and `player` commands. Check the device node for the serial adapter, for example:

```sh
ls -l /dev/ttyUSB0
```

The capture user must be allowed to read and write that node. If it is owned by a group such as `dialout`, add the capture user to that exact group:

```sh
sudo usermod -aG dialout captureuser
```

Replace both names with the group and login name reported by the host. Start a fresh login session afterward so the new group membership is active. If local policy uses a udev rule for serial adapters instead, ensure its final permissions give the capture user equivalent access.

## Before the First Capture

After completing the persistent changes and starting a fresh login session, verify all three settings together:

```sh
cat /sys/module/usbcore/parameters/usbfs_memory_mb
ulimit -S -l
ulimit -H -l
ulimit -S -r
ulimit -H -r
./build/dddcli list-devices
```

If capture reports `USB memory limit` or `LIBUSB_ERROR_NO_MEM`, re-check USBFS memory. If it says `RLIMIT_MEMLOCK` or `mlock failed`, re-check both the soft and hard locked-memory values. If it warns that realtime priority could not be set, capture may still work, but long captures are more exposed to CPU scheduling delays until the realtime limits are corrected.

`--small-usb-transfer-queue` is a fallback for hosts that cannot be tuned; it is not the preferred default-queue configuration.
