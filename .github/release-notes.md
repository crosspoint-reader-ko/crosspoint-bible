Xteink X4 / X3 (ESP32-C3) firmware with the 개역한글 (1952/1961, public domain) Bible built in.

**Install**: put `crosspoint-bible-*-x4-x3.bin` on the SD card and flash it from Settings > System > SD firmware update (on CrossPoint firmware), or over USB with esptool (`esptool.py --chip esp32c3 write_flash 0x10000 <file>`).

**Another translation**: convert your own EPUB or getBible JSON with `epub2cpb.py` and copy the `.cpb` into `/bible/` on the SD card. A pack on the SD card takes precedence over the built-in one. `krv1961.cpb` is the built-in pack, for reference.
