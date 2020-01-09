## Summary
midiccmap allows to map midi continuous controllers to
- other cc
- nrpn
- rpn
- pitch bend
cc are 7-bit values, while nrpn, rpn and pb are 14-bit
This means a scaling has to be applied
The output will only have 128 distinct values anyway
The output resolution or range has to be sacrified

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
