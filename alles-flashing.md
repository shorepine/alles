# Alles Flashing HOWTO

This page will help you upgrade your own firmware on an Alles hardware speaker.

## Upgrading firmware wirelessly

**If you have a recent model (you received it after November 2021), or flashed your firmware after November 2021, you can upgrade your firmware over the internet.**

After you first set up wifi (and make sure your speaker is on an wifi that can connect to the internet,) if you turn off then on the speaker, then press the + button while the "joining wifi" tones are playing (before it plays the turn-on bleep), the speaker will connect to the internet and download the latest Alles speaker firmware right to the device, then reboot. It takes about a minute, and you'll hear a different repeating tone while it works. 


## Upgrading firmware over USB

If you want to write your own Alles firmware, or have a speaker before December 2021 and need to get it to support wireless upgrading, follow these directions:

The back of the hardware speaker has a micro-USB plug you use for charging. It also can be used to upgrade the firmware. Once you upgrade an Alles speaker to a recent version, you can use the OTA flashing method going forward. 

To do this, you need to set up the following things:

### Set up ESP-IDF

ESP-IDF is the set of open source tools and libraries that work on the CPU powering Alles, the ESP32. You should first install ESP-IDF on your system if you haven't already. We tend to use the master branch of the IDF as we use some newer features.

You should follow [the instructions to download and set up `esp-idf`](http://esp-idf.readthedocs.io/en/latest/get-started/). 

### Get the UART drivers

If using macOS, you'll want to also install the [CP210X drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) if you haven't already. 

### Get the Alles repository

You likely already have this, but if not, [download and unzip this repository](https://github.com/bwhitman/alles/archive/refs/heads/main.zip).

### Get a real micro-USB cable 

The cable you may have received from us for the hardware speaker is charge only. Find a longer micro-USB cable that transmits data and connect it to the computer and speaker. 

### Flash

Then, in the `esp` folder you created during installing the ESP-IDF above, run `. ./esp-idf/export.sh`. Now cd into the alles repository folder and run `idf.py flash` to build and flash to the board. It will take a couple of minutes and show you progress. The board will reboot into the latest firmware. 

(If the flashing process doesn't work, it's likely not finding your UART location. Type `ls /dev/*usb*` to find something like `/dev/tty.usbserial.XXXXX` or `/dev/cu.usbserialXXXX`.  You'll want to find the tty that appears when you connect the speaker to computer. Copy this location and try flashing again with `idf.py -p /dev/YOUR_SERIAL_TTY flash`.)


### Monitor

If you want to see debugging messages, use `idf.py -p /dev/YOUR_SERIAL_TTY monitor` to reboot the board and see stdout/stderr. Use Ctrl-] to exit the monitor.



