## Summary

Apply pitch bend depending on last note.

This allows microtuning on mono synths that do not provide it.
This works properly only in mono mode: because pitch bend is a channel message, if a chord is played the same pitch bend will be applied to all notes.

## Installation

No installer provided at the moment

The only prerequisites are alsa and fltk

The following commands were tested under Linux Mint 19.1

- Installing ALSA development files:
```
sudo apt install libasound2-dev
```
- Installing FLTK and its development files:
```
sudo apt install libfltk1.3-dev
```
- Compiling:
```
gcc autobend.c -o autobend -lfltk -lasound -lpthread -lstdc++
```
- Installing:
```
sudo cp autobend /usr/local/bin/
sudo cp po/fr/autobend.mo /usr/share/locale/fr/LC_MESSAGES/
```
## Usage
```
autobend [file]
```
Using the sliders with the mouse gives only coarse control, accuracy requires using the keyboard shortcuts
up arrow or numeric keypad +, down arrow or numeric keypad -, page up and down or using the mouse scroll wheel.
Delete or ., home and end will set the value to 0, -8192 and +8191 respectively.

Autobend has no way of knowing the bender range on your synth,
the pitch bend midi message range is fixed from -8192 to 8191
but the corresponding musical interval may vary, therefore tuning has to be done by ear.

Files default to .conf file type. This is a plain text file, whith a very simple syntax:
each line is of the form "note space offset", for example E -2048.

### Thanks
Thanks to jmechmech for the original idea and testing
