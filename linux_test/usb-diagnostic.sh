#!/bin/sh
#
# beyond2lte Linux/Android USB diagnostic collector.
#
# This script is deliberately read-only with respect to MAX77705 and USB
# hardware.  It never uses i2cget/i2cset and never writes MMIO.  The before / 
# after snapshots let us compare a working Samsung Linux stack with the
# Windows driver without spending another flash on speculative register
# changes.
#
# Usage on a rooted Linux/Android boot:
#   sh usb-diagnostic.sh --guided ./B2LUSB-LINUX.TXT
#   sh usb-diagnostic.sh --once   ./B2LUSB-LINUX.TXT
#

MODE=guided
case "${1:-}" in
    --guided|--once)
        MODE=${1#--}
        shift
        ;;
esac

OUT=${1:-./B2LUSB-LINUX.TXT}

say() {
    echo "B2LUSB-LINUX: $*"
    if [ -c /dev/kmsg ]; then
        echo "B2LUSB-LINUX: $*" > /dev/kmsg 2>/dev/null || true
    fi
}

read32() {
    if command -v devmem >/dev/null 2>&1; then
        devmem "$1" 32 2>/dev/null
    elif command -v busybox >/dev/null 2>&1 &&
         busybox --list 2>/dev/null | grep -qx devmem; then
        busybox devmem "$1" 32 2>/dev/null
    else
        return 1
    fi
}

mmio32() {
    MMIO_VALUE=$(read32 "$2" 2>/dev/null) || MMIO_VALUE=unavailable
    printf '%-28s %-12s %s\n' "$1" "$2" "$MMIO_VALUE"
}

dump_attr() {
    [ -r "$1" ] || return 0
    printf '%s = ' "$1"
    tr '\n' ' ' < "$1" 2>/dev/null
    echo
}

dump_class() {
    CLASS_ROOT=$1
    shift
    [ -d "$CLASS_ROOT" ] || {
        echo "$CLASS_ROOT: absent"
        return
    }

    echo "$CLASS_ROOT:"
    ls -la "$CLASS_ROOT" 2>/dev/null
    for CLASS_DEVICE in "$CLASS_ROOT"/*; do
        [ -e "$CLASS_DEVICE" ] || continue
        echo "[$CLASS_DEVICE -> $(readlink -f "$CLASS_DEVICE" 2>/dev/null)]"
        for CLASS_ATTR in "$@"; do
            dump_attr "$CLASS_DEVICE/$CLASS_ATTR"
        done
    done
}

dump_i2c_devices() {
    echo "########## I2C devices relevant to USB-C ##########"
    for I2C_DEVICE in /sys/bus/i2c/devices/*; do
        [ -e "$I2C_DEVICE" ] || continue
        I2C_NAME=$(cat "$I2C_DEVICE/name" 2>/dev/null)
        I2C_MODALIAS=$(cat "$I2C_DEVICE/modalias" 2>/dev/null)
        case "$(basename "$I2C_DEVICE") $I2C_NAME $I2C_MODALIAS" in
            *[Mm][Aa][Xx]77705*|*[Cc][Cc][Ii][Cc]*|*[Mm][Uu][Ii][Cc]*|\
            *[Uu][Ss][Bb][Cc]*|*[Tt][Yy][Pp][Ee][Cc]*|*[Pp][Dd][Ii][Cc]*)
                echo "[$I2C_DEVICE]"
                echo "name=$I2C_NAME"
                echo "modalias=$I2C_MODALIAS"
                echo "driver=$(readlink -f "$I2C_DEVICE/driver" 2>/dev/null)"
                cat "$I2C_DEVICE/uevent" 2>/dev/null
                ;;
        esac
    done
}

dump_platform_devices() {
    echo "########## platform devices relevant to USB ##########"
    for PLATFORM_DEVICE in /sys/bus/platform/devices/*; do
        [ -e "$PLATFORM_DEVICE" ] || continue
        PLATFORM_NAME=$(basename "$PLATFORM_DEVICE")
        PLATFORM_DRIVER=$(readlink -f "$PLATFORM_DEVICE/driver" 2>/dev/null)
        case "$PLATFORM_NAME $PLATFORM_DRIVER" in
            *[Uu][Ss][Bb]*|*[Dd][Ww][Cc]3*|*[Xx][Hh][Cc][Ii]*|*[Pp][Hh][Yy]*|\
            *[Oo][Tt][Gg]*|*[Tt][Yy][Pp][Ee][Cc]*|*[Ee][Xx][Tt][Cc][Oo][Nn]*|\
            *[Hh][Ss][Ii]2[Cc]*|*[Mm][Aa][Xx]77705*|*[Cc][Cc][Ii][Cc]*|*[Mm][Uu][Ii][Cc]*)
                echo "$PLATFORM_NAME driver=$PLATFORM_DRIVER"
                ;;
        esac
    done
}

dump_usb_devices() {
    echo "########## USB bus devices ##########"
    for USB_DEVICE in /sys/bus/usb/devices/*; do
        [ -e "$USB_DEVICE" ] || continue
        echo "[$USB_DEVICE -> $(readlink -f "$USB_DEVICE" 2>/dev/null)]"
        dump_attr "$USB_DEVICE/busnum"
        dump_attr "$USB_DEVICE/devnum"
        dump_attr "$USB_DEVICE/speed"
        dump_attr "$USB_DEVICE/idVendor"
        dump_attr "$USB_DEVICE/idProduct"
        dump_attr "$USB_DEVICE/product"
        dump_attr "$USB_DEVICE/manufacturer"
        dump_attr "$USB_DEVICE/authorized"
        cat "$USB_DEVICE/uevent" 2>/dev/null
    done

    if [ -r /sys/kernel/debug/usb/devices ]; then
        echo "########## debugfs USB devices ##########"
        cat /sys/kernel/debug/usb/devices
    fi
}

dump_hardware_registers() {
    echo "########## read-only Exynos9820 register snapshot ##########"
    echo "-- HSI2C45 / USI (MAX77705 transport)"
    mmio32 HSI2C_CTL               0x15D60000
    mmio32 HSI2C_FIFO_CTL          0x15D60004
    mmio32 HSI2C_TRAIL_CTL         0x15D60008
    mmio32 HSI2C_CLK_CTL           0x15D6000C
    mmio32 HSI2C_CLK_SLOT          0x15D60010
    mmio32 HSI2C_INT_EN            0x15D60020
    mmio32 HSI2C_INT_STAT          0x15D60024
    mmio32 HSI2C_ERR_STAT          0x15D6002C
    mmio32 HSI2C_FIFO_STAT         0x15D60030
    mmio32 HSI2C_CONF              0x15D60040
    mmio32 HSI2C_AUTO_CONF         0x15D60044
    mmio32 HSI2C_TIMEOUT           0x15D60048
    mmio32 HSI2C_TRANS_STAT        0x15D60050
    mmio32 HSI2C_TIMING_FS1        0x15D60060
    mmio32 HSI2C_TIMING_FS2        0x15D60064
    mmio32 HSI2C_TIMING_FS3        0x15D60068
    mmio32 HSI2C_TIMING_SLA        0x15D6006C
    mmio32 HSI2C_ADDR              0x15D60070
    mmio32 HSI2C_USI_CON           0x15D600C4
    mmio32 CMGP_USI03_MODE         0x15C22030

    echo "-- USB PHY / PMU"
    mmio32 USBPHY_LINK_CTRL        0x10B00004
    mmio32 USBPHY_LINK_PORT        0x10B00008
    mmio32 USBPHY_CLKRST           0x10B00020
    mmio32 USBPHY_DUALPHYSEL       0x10B00028
    mmio32 USBPHY_UTMI             0x10B00050
    mmio32 USBPHY_HSP              0x10B00054
    mmio32 USBPHY_HSP_TUNE         0x10B00058
    mmio32 USBPHY_HSP_TEST         0x10B0005C
    mmio32 PMU_USB_PHY_CTRL        0x1586072C

    echo "-- DWC3"
    mmio32 DWC3_GCTL               0x10C0C110
    mmio32 DWC3_GSTS               0x10C0C118
    mmio32 DWC3_GSNPSID            0x10C0C120
    mmio32 DWC3_GUSB2PHYCFG0       0x10C0C200
    mmio32 DWC3_GUSB3PIPECTL0      0x10C0C2C0
    mmio32 DWC3_DCTL               0x10C0C704
    mmio32 DWC3_DSTS               0x10C0C70C

    echo "-- xHCI (operational base derived from CAPLENGTH)"
    XHCI_CAP=$(read32 0x10C00000 2>/dev/null) || XHCI_CAP=
    if [ -n "$XHCI_CAP" ]; then
        XHCI_CAPLEN=$((XHCI_CAP & 255))
        XHCI_OP=$((0x10C00000 + XHCI_CAPLEN))
        mmio32 XHCI_CAPLENGTH       0x10C00000
        mmio32 XHCI_USBCMD          "$(printf '0x%X' "$XHCI_OP")"
        mmio32 XHCI_USBSTS          "$(printf '0x%X' $((XHCI_OP + 4)))"
        mmio32 XHCI_CONFIG          "$(printf '0x%X' $((XHCI_OP + 0x38)))"
        mmio32 XHCI_PORTSC1         "$(printf '0x%X' $((XHCI_OP + 0x400)))"
        mmio32 XHCI_PORTSC2         "$(printf '0x%X' $((XHCI_OP + 0x410)))"
    else
        echo "xHCI registers unavailable"
    fi
}

snapshot() {
    SNAPSHOT_NAME=$1
    {
        echo
        echo "============================================================"
        echo "SNAPSHOT=$SNAPSHOT_NAME"
        echo "DATE=$(date 2>/dev/null)"
        echo "UPTIME=$(cat /proc/uptime 2>/dev/null)"
        echo "CMDLINE=$(cat /proc/cmdline 2>/dev/null)"
        uname -a 2>/dev/null

        echo "########## filtered kernel log ##########"
        dmesg 2>/dev/null | grep -i -E \
            'max77705|ccic|muic|type.?c|usb.?role|dwc3|xhci|exynos.*usb|usb.*phy|vbus|otg|extcon|usbc|hsi2c|s2mps19|ldo1[012]|i2c.*(fail|error|timeout|nack)' \
            | tail -300

        dump_class /sys/class/typec data_role power_role port_type \
            preferred_role orientation power_operation_mode \
            usb_power_delivery_revision
        dump_class /sys/class/usb_role role
        dump_class /sys/class/udc state function current_speed maximum_speed
        dump_class /sys/class/extcon name state
        dump_class /sys/class/power_supply type online present status usb_type
        dump_i2c_devices
        dump_platform_devices
        dump_usb_devices

        echo "########## relevant interrupts ##########"
        grep -i -E 'max77705|ccic|muic|usb|dwc|xhci|hsi2c|i2c' \
            /proc/interrupts 2>/dev/null

        if [ -r /sys/kernel/debug/regulator/regulator_summary ]; then
            echo "########## relevant regulators ##########"
            grep -i -E 'usb|vbus|otg|max77705|ccic|muic|m_vdd_ldo1[012]|ldo1[012]' \
                /sys/kernel/debug/regulator/regulator_summary 2>/dev/null
        fi

        if [ -r /sys/kernel/debug/gpio ]; then
            echo "########## relevant GPIOs ##########"
            grep -i -E 'usb|vbus|otg|max77705|ccic|muic' \
                /sys/kernel/debug/gpio 2>/dev/null
        fi

        dump_hardware_registers
    } >> "$OUT" 2>&1
}

: > "$OUT" || {
    say "cannot create output: $OUT"
    exit 1
}

mkdir -p /sys/kernel/debug 2>/dev/null || true
if ! grep -q ' /sys/kernel/debug ' /proc/mounts 2>/dev/null; then
    mount -t debugfs none /sys/kernel/debug 2>/dev/null || true
fi

say "collector is read-only; no MAX77705/I2C/MMIO writes will be made"

if [ "$MODE" = guided ]; then
    say "REMOVE the USB receiver/cable now; baseline in 10 seconds"
    sleep 10
    snapshot BEFORE_UNPLUGGED

    say "PLUG the USB receiver/mouse now; second snapshot in 20 seconds"
    sleep 20
    snapshot AFTER_PLUGGED
else
    snapshot ONCE
fi

sync 2>/dev/null || true
say "complete: $OUT ($(wc -c < "$OUT" 2>/dev/null) bytes)"
