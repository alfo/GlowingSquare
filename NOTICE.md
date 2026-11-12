# Notice

esp32-iot-template is derived from **SpoolmanScale** by Niko11111
(<https://github.com/Niko11111/SpoolmanScale>), licensed under the GNU General
Public License v3.0. The WiFi manager, setup portal, Improv serial, settings
registry, web interface, OTA and release pipeline, logging and the LVGL shell
started as SpoolmanScale's code; the filament, NFC, scale and backend code was
removed and the rest generalised.

This template, and any project built from it, is therefore distributed under
the GNU General Public License v3.0 as well (see LICENSE).

## Glowing Square

Glowing Square 2 is built on esp32-iot-template (above). It also contains:

- `src/project/patterns/`: the Aurora patterns by Jason Coon
  (<https://github.com/pixelmatix/aurora>), MIT licence, with portions by
  Stefan Petrick; see the header of each file.
- `lib/PxMatrix/`: PxMatrix by Dominic Buchstaller
  (<https://github.com/2dom/PxMatrix>), BSD licence (lib/PxMatrix/license.txt),
  with changes for Arduino core 3 marked `// GS:`.
