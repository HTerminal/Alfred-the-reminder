#!/bin/bash
# ReminderESP32 flasher — macOS (double-click me in Finder)
cd "$(dirname "$0")" || exit 1

echo
echo "  ============================================================"
echo "   ReminderESP32 flasher  -  Waveshare ESP32-C6-LCD-1.69"
echo "  ============================================================"
echo
echo "  1) Plug the board into this Mac with a USB-C cable."
echo "  2) Press Enter to flash."
echo "  (Optional) pass the port if auto-detect fails, e.g."
echo "       ./flash-mac.command /dev/tty.usbmodem1101"
echo
read -r _

# clear macOS 'downloaded from internet' quarantine so esptool can run, then make it executable
xattr -dr com.apple.quarantine . 2>/dev/null
chmod +x ./esptool 2>/dev/null

PORTARG=""
[ -n "$1" ] && PORTARG="--port $1"

./esptool --chip esp32c6 $PORTARG --baud 921600 write_flash -z 0x0 firmware-merged.bin
RC=$?

echo
if [ "$RC" = "0" ]; then
  echo "  [DONE] Firmware flashed. The device restarts on its own."
  echo "         Open the IP shown on its screen to configure it."
else
  echo "  [FAILED] exit code $RC"
  echo "   - Is the board plugged in? Close any serial monitor."
  echo "   - If macOS blocks 'esptool', right-click it in Finder > Open once, then re-run."
  echo "   - If the port isn't found, re-run with it, e.g.  ./flash-mac.command /dev/tty.usbmodem1101"
fi
echo
echo "Press Enter to close."
read -r _
