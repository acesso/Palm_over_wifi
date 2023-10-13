# Palm Serial Over Wifi

The main goal of this project is to use ESP based device (ESP32 in this case) as a brige for old (really old) palm devices such as Pilot and first generations that lack iR or any wireless capacilities.

It currently "Emulates" a DCE serial modem to fake a real old style connection, as soon the handshake completes all data is sent over a TCP socket. The other end of the socket can be anything reachable from ESP wifi.

## Hardware

Since ESP uses TTL lines, a MAX232(or similar) needs to be used to convert RS232 signaling.

Currently a Palm Pilot personal is the only device I have for testing, but others from the same generation should be compatible.

## TCP to PTY socket

At the host endpoint a TCP socket can be mapped into a PTY device using [socat](http://www.dest-unreach.org/socat/), allowing any palm compatible software (jpilot and its friends) to use it as a regular serial device.

```bash
socat -d -d -d TCP4-LISTEN:6666,reuseaddr,fork PTY,link=/tmp/palm,rawer
```

## Build

Use [Espressif FreeRTOS](https://github.com/espressif/esp-idf) version >=5 to build and optnionally push to ESP.

## Acknowledgments

- Source of inpiration - https://palm2000.com/projects/bringingTheInternetWirelesslyToAPalmM100.php
- US Robotics 56k DCE documentation - https://support.usr.com/support/5631/5631-de-ug/syntax.html
- Palm organizer pinouts - https://www.rigacci.org/pub/doc/palm/electrical_interface(10-pin)signals.pdf
- Serial programing - https://en.wikibooks.org/wiki/Serial_Programming/Modems_and_AT_Commands