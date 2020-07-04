# Glowing Square

This project is a purpose-built display based on a 64x32 LED matrix and an ESP32. The code can be easily adapted for other purposes, and includes code to fetch web requests, store config, etc.

I have made three displays so far: one that shows the current London Tube departures for my local station with data from TfL, one that shows planes flying over my house with data from Flightradar24 based on [this project](https://github.com/GroundBreakingItem0/esp8266_plane_tracker), and one that shows network statistics for my Unifi-based home network.

![Photo](https://i.imgur.com/ZX31npw.jpg)

Full photo album [here](https://imgur.com/a/xTvUx8F).

## Features

* Fetches JSON data from an API at a given interval
* Parses the JSON and displays it
* Brightness controlled by MQTT
* "Party Mode" / Screensaver mode, turned on and off by MQTT
* WiFiManager-based hotspot for configuration

## Hardware Required
* 64x32 P4 LED Matrix (from [here](https://www.aliexpress.com/store/group/P4-led-module/1848738_509034839.html?spm=a2g0o.detail.0.0.65cc64c7oVbRJF))
* D1 Mini 32 (any of [these](https://www.aliexpress.com/wholesale?catId=0&initiative_id=SB_20200704011236&SearchText=mini+esp32))
* Optional: Brian Lough's Shield PCB (you can wire it yourself instead, but these are much neater) (from [here](https://www.tindie.com/products/brianlough/esp32-matrix-shield-mini-32/))
* 24V PSU (up to 35W per display) (I picked [this one](https://www.ebay.co.uk/itm/DC-5V-12V-24V-Universal-Regulated-Switching-Power-Supply-for-LED-Strip-CCTV-UK/162562112782) for 3 displays) 
* LM2596 Buck Converter to step down to 5V (from [here](https://www.aliexpress.com/wholesale?catId=0&initiative_id=SB_20200704011644&SearchText=LM2596))
* Two-core cable, Wago connectors/screw connectors, etc.
* 6x M3 12mm bolts, 16x 3mm short countersunk screws

You can just use a 5V power supply instead, but 100W+ ones tend to have loud fans in them, which is annoying, whereas my 24V 120W PSU is completely silent. It also helps with voltage drop if you're using long cables.

## Enclosure

See the files in the Enclosure folder for the STLs of the two 3D-printed parts of this project, as well as the full Fusion 360 model showing you how it all fits together and what wood is required for the project.

![Back view](https://i.imgur.com/OPJ0tCZ.jpg)

## Global Config Options

These options are available across all three displays:

* Hostname - what your device will show up in your router admin console as (can't contain spaces, etc)
* MQTT Server - the IP or domain name of your MQTT server
* MQTT Port - defaults to 1833
* MQTT Username
* MQTT Password

## MQTT Control

Set the room name (in the format `living_room` for example) in the web config interface, along with your MQTT server and port, etc. This will become the base of the topic when combined with the `MQTT_NAME` of your device in `mqtt.h`. Then the device will:

* Publish on `room_name/MQTT_NAME/mqtt` `connected` or `disconnected` as a last will and testament
* Listen on `room_name/MQTT_NAME/in` for messages in JSON format (e.g. `{ "brightness": 160 }` and will fade to the new brightness
* Listen on `room_name/state` for messages in JSON format (e.g. `{ "party_mode": "chill" }` (or `off` or `on`)

## Tube / London Bus

Find your TfL Stop ID [here](https://api.tfl.gov.uk/swagger/ui/index.html?url=/swagger/docs/v1#!/StopPoint/StopPoint_Search). TfL treats tube stops and bus stops the same, so if you provide the correct route and stop ID, it will work for both.

Config Options:

* TfL Stop ID
* TfL Route (e.g. `northern`, `circle`, `H91`, `9`), you can test your route [here](https://api.tfl.gov.uk/swagger/ui/index.html?url=/swagger/docs/v1#!/Line/Line_Arrivals)
* Direction (options are `outbound`, `inbound`, or empty for both)

## Flights

Use Google Maps or similar to find the longitude and latitude of the top left and bottom right points that define the area you want to monitor flights inside.

Config Options: 

* Flight Area in the format `51.5672,51.4131,-0.4146,0.1107` (for example)

## Unifi

Unlike the other two, this one requires a PHP script running on a web server (because Unifi needs 3 different API requests to fetch the info I wanted to display, and also Unifi returns JSON payloads that are too big to fit in the RAM of the ESP32).

Install the `GlowingSquare/Unifi/web/` files on your PHP server, and rename the `config.example.php` to `config.php`, filling in the necessary details as outlined by the code comments. Test that your server is working by visiting it in a web browser with `?width=64` appended to the URL to check you can scrape data correctly.

Config Options:

* Script URL as set up above, e.g. `http://10.0.1.9/glowingsquare/?width=64`

## Other Notes

This project was just started as a passtime during the 2020 COVID-19 lockdown as a bit of fun, happy to take questions but this isn't a long-term open source kinda deal, I'm just open-sourcing an already completed thing.

People have asked me if I'll be selling these, and I'll consider it and come up with a rough price estimate. It's a lot easier if you don't bother with the nice enclosure, which takes roughly 30% of the cost.
