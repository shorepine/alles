# Alles Speaker RevB getting started notes

Hello! You're the lucky owner of at least one Alles speaker. This guide will help you get started.

## The Speaker

Each Alles speaker has four buttons up top and a USB micro receptacle on back. 

Before getting going, let's make sure the speaker is charged or charging. Simply plug a USB cable between the speaker and any USB charge point, like a phone charger or a port on a computer. (The speaker will show up as a USB device for debugging and upgrades, but you don't have to worry about that for now -- all control happens wirelessly.) The battery will last "a few hours" making constant music and longer with sparse or silence. It shouldn't take more than an hour or so to charge. If the power button does not make any noise, it's probably a dead battery.

The row of buttons up top, facing the speaker, are power, MIDI (+), WiFi (-) and restart (Play/Pause).

![Picture of speaker](https://raw.githubusercontent.com/bwhitman/alles/master/pics/revb-top.png)

The power button turns the speaker off and on. When you turn it on, it will re-join the network and mesh. 

The MIDI button (+) will put the speaker into MIDI mode. In this mode, Bluetooth MIDI is activated and the speaker will stay silent and act as a MIDI relay to the rest of the mesh. So you will need at least two Alleses to use MIDI mode and hear audio. Press MIDI again to restart into normal mode.

The WiFi button (-) will forget the WiFi details, if any were stored, and put the device into WiFi setup mode. This is the default if no WiFi has yet been set. 

The restart (Play/Pause) button simply restarts the speaker. 


## Join a WiFi network

If this is your first time using the speaker, you'll need to tell it which WiFi network to join. You can easily change this later, but once you set it, you don't need to set it again. 

Press the power button. You'll start to hear a "chime noise" repeating. This is the "searching for WiFi" sound, and will play until it finds a WiFi point. If you've already set a WiFi network, after about a few seconds the chime noise will end and you'll hear a "bleep" tone and then silence. That means the WiFi network has been joined and everything is ready to go.

If you haven't set WiFi yet, now open your nearest mobile phone or any WiFi device with a browser. Go to join a new WiFi network on your device and you'll see a network listed called `alles-synth-XXXXXX` where `XXXXXX` is a unique string per speaker (useful when you have many speakers!). Join that network. On most devices (iPhones, Androids especially) after a few seconds, a browser window will appear with a login page. This is like when you join a hotel's network or other captive portal. If the page never appears, try going to `http://10.10.0.1` in your browser after joiining the network.

![Alles WiFi settings](https://raw.githubusercontent.com/bwhitman/alles/master/pics/alles-wifi.png)

Wait a few seconds for the login page to populate with all the nearby WiFi stations it finds. After you see the one you want the speaker to join, tap it and carefully enter its password. After a few moments, the speaker should stop chiming, indicating it has succesfully joined the network and saved the details to its internal storage. 

If this doesn't seem to work, try again by hitting the WiFi button (-) and the process will repeat again. 


## Control Alles

There are two main ways to control your speakers. We call them *direct mode* and *MIDI mode*. 

Direct mode is sending the mesh explicit messages that define the state of oscillators. You can control anything you can imagine, with high precision and millisecond accuracy. You can control up to 64 oscillators on each of any number of speakers in a mesh. You do this from either a programming language like Python (what we use) or an environment like Max/MSP (or Max for Ableton Live). Python is built-in on Macs and pretty easy to use once you get the hang of it. And then you can write small programs to make interesting sounds! For this tutorial, we'll use Python. But there's also a Max patch you can download that shows you how to access all the same parameters as we're changing in Python. So it's up to you!

There's also MIDI mode. If you want to just treat the mesh as a synthesizer using your existing setup, MIDI mode is for you. You have less control over all the things you can change, but you'll still be able to control up to 15 speakers in a mesh and play with preset tones and change some parameters using MIDI CCs. You can start MIDI mode by pushing the MIDI button and then connecting your computer or mobile device to one speaker using Bluetooth MIDI. That single speaker will stop playing audio and will become a controller for the rest of the mesh. Any MIDI messages it receives will be re-broadcasted out to the mesh. 

So let's start python. On your Mac, open Terminal.app. Make sure you are in the directory containing this repository, e.g. `cd Downloads/alles-main`. And type `python3`. If you've never done this sort of thing before, you may have to accept a small download of tools from Apple the first time you run Python. Let that finish. Then you'll see a prompt like `>>>`. 

Start by importing the Python modules needed to control the mesh: `import alles, amy`


### Simple examples

`alles.drums()` should play a test pattern out of all the currently turned-on speakers. They should all be in sync and playing the same thing.

When you want the speakers to be quiet, use `amy.reset()`. That resets all speakers to defaults. 


```
alles.preset(0, osc=2) # will set a simple sine wave tone on oscillator 2
amy.send(osc=2, note=50, vel=1.5) # will play the note at velocity 1.5
amy.send(osc=2, vel=0) # will send a "note off" -- you'll hear the note release
amy.send(osc=2, freq=220.5, vel=1.5) # same but specifying the frequency
amy.reset()
```

### Multiple speakers



### Additive synthesis 

```
amy.reset()
amy.send(osc=0,vel=1,note=50,wave=amy.PARTIALS,patch=5) # a nice organ tone
amy.send(osc=0,vel=1,note=50,wave=amy.PARTIALS,patch=6,ratio=0.2) # ratio slows down the partial playback
```

advanced, diy

```
brew install ffmpeg
python3 -m pip install pydub --user
tar xvf loris-1.8.tar
cd loris-1.8
CPPFLAGS=`python3-config --includes` PYTHON=`which python3` ./configure --with-python
make
sudo make install

```

```
import partials


```


### Fun with frequency modulation

Let's make the classic FM bell tone ourselves, without a preset. We'll just be using two operators (two sine waves), one modulating the other. 

```
amy.reset()
amy.send(wave=amy.SINE,ratio=0.2,amp=0.1,osc=0,bp0_target=amy.TARGET_AMP,bp0="1000,0,0,0")
amy.send(wave=amy.SINE,ratio=1,amp=1,osc=1)
amy.send(wave=amy.ALGO,algorithm=0,algo_source="-1,-1,-1,-1,1,0",osc=2)
```

Let's unpack that last line: we're setting up a ALGO "oscillator" that controls up to 6 other oscillators. We only need two, so we set the `algo_source` to mostly -1s (not used) and have oscillator 1 modulate oscillator 0. You can have the operators work with each other in all sorts of crazy ways. For this simple example, we just use the DX7 algorithm #1 (but we count from 0, so it's algorithm 0). And we'll use only operators 2 and 1. Therefore our `algo_source` lists the oscillators involved, counting backwards from 6. We're saying only have operators 2 and 1, and have oscillator 1 modulate oscillator 0. 

![DX7 Algorithms](https://raw.githubusercontent.com/bwhitman/alles/master/pics/dx7_algorithms.jpg)

What's going on with `ratio`? And `amp`? Ratio, for FM synthesis operators, means the ratio of the frequency for that operator and the base note. So oscillator 0 will be played a 20% of the base note, and oscillator 1 will be the frequency of the base note. And for `amp`, that's something called "beta" in FM synthesis, which describes the strength of the modulation. Note we are having beta go down over 1,000 milliseconds using a breakpoint. That's key to the "bell ringing out" effect. 

Ok, we've set up the oscillators. Now, let's hear it!

```
>>> amy.send(osc=2, note=50, vel=3)
```

You should hear a bell-like tone. Nice.

Another classic two operator tone is to instead modulate the higher tone with the lower one, to make a filter sweep:

```
amy.reset()
amy.send(osc=0,ratio=0.2,amp=0.5,bp0_target=amy.TARGET_AMP,bp0="0,0,5000,1,0,0")
amy.send(osc=1,ratio=1)
amy.send(osc=2,algorithm=0,wave=amy.ALGO,algo_source="-1,-1,-1,-1,0,1")
```

Then play it with 

```
amy.send(osc=2,vel=2,note=50)
```

Nice. You can see there's limitless ways to make interesting evolving noises. 


## Advanced section


### Alles on your computer

```
python3 -- will auto-require xcode tools download on big sur 
homebrew: /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install soundio
make
./alles
```

### Updating the Alles firmware







