## Summary
midiccmap allows to map midi continuous controllers, pitch bend and aftertouch to
- cc
- nrpn (non registered parameter number)
- rpn (registered parameter number)
- pitch bend
- aftertouch

cc and aftertouch are 7-bit values, while nrpn, rpn and pb are 14-bit.

This means a scaling has to be applied.

The output can only have 128 distinct values when the source is 7-bit.

When the source is 7-bit and the destination 14-bit,

the output resolution and/or range have to be reduced.

Default scaling maps to full output range.

## Installation

No installer provided at the moment

The only prerequisite is alsa

The following commands were tested under debian 9

- Installing ALSA development files:
```
sudo apt install libasound2-dev
```
- Compiling:
```
gcc -o midiccmap midiccmap.c -lasound
```
- Installing:
```
sudo cp midiccmap /usr/local/bin/
```
## Usage
```
midiccmap -h
```
will display available options.

Mapping can be set on command line or in .ini file,

Scaling can only be set in .ini file.

See midiccmap.ini for commented examples.

## Thanks
Thanks to jmechmech for the original idea and testing
