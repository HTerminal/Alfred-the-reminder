#!/bin/bash
# ReminderESP32 flasher — Linux
cd "$(dirname "$0")" || exit 1

echo
echo "  ============================================================"
echo "   ReminderESP32 flasher  -  Waveshare ESP32-C6-LCD-1.69"
echo "  ============================================================"
echo
echo "  1) Plug the board into this PC with a USB-C cable."
echo "  2) Press Enter to flash."
echo "  (Optional) pass the port if auto-detect fails, e.g."
echo "       ./flash-linux.sh /dev/ttyACM0"
echo
echo "  NOTE: serial access may need your user in the 'dialout' group:"
echo "        sudo usermod -a -G dialout \$USER   (then log out and back in)"
echo "        ...or just run this script with sudo."
echo
read -r _

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
  echo "   - Permission denied on the port? Add yourself to 'dialout' (see above) or use sudo."
  echo "   - If the port isn't found, re-run with it, e.g.  ./flash-linux.sh /dev/ttyACM0"
fi
echo
read -r _
