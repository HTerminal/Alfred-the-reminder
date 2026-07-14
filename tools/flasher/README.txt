ReminderESP32 — one-click flasher
=================================

This folder flashes the ReminderESP32 firmware onto a
Waveshare ESP32-C6-LCD-1.69 board. Nothing to install — esptool is included.

WHAT'S INSIDE
  firmware-merged.bin   the compiled firmware (bootloader + partitions + app)
  esptool / esptool.exe the flashing tool (bundled)
  flash-*               the click-to-run flasher for your OS

HOW TO USE
  Windows :  double-click  flash-windows.bat
  macOS   :  double-click  flash-mac.command   (right-click > Open the first time)
  Linux   :  run           ./flash-linux.sh    (chmod +x it if needed)

  1. Plug the board into your computer with a USB-C cable.
  2. Run the flasher for your OS. It auto-detects the port.
  3. When it finishes, the board restarts. The IP address shown on its
     screen is the config page — open it in a browser to set reminders,
     sounds, icons, sleep schedule, etc.

IF IT CAN'T FIND THE PORT
  Pass the port as an argument, e.g.
     Windows :  flash-windows.bat COM8
     macOS   :  ./flash-mac.command /dev/tty.usbmodem1101
     Linux   :  ./flash-linux.sh /dev/ttyACM0
  Still stuck? Put the board in download mode: hold BOOT, tap RESET,
  release BOOT, then run the flasher again.

NOTES
  - This flashes the firmware only; your saved settings, uploaded sounds
    and icons on the device are preserved. New sounds/icons are added from
    the web config page (no reflashing needed).
  - macOS may block the bundled esptool the first time (Gatekeeper). Right-
    click esptool > Open once, or the script clears the quarantine for you.
  - Linux serial access may need: sudo usermod -a -G dialout $USER
