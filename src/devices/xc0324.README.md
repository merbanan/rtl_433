

### The Digitech XC0324 device

#### Introduction

This README.md file:

* documents the protocol used by the Digitech XC-0324 temperature sensor

and then continues with a "tutorial for my future self" about how the
`xc0324_callback` handler was developed.  The tutorial is
written "by a newbie, for a newbie" so may well be too verbose for
people with experience, either as a developer or with SDR.  But it might
be useful as starting point for someone who has just come across `rtl_433`
and is interested in trying to reverse engineer a new device.

In **hindsight** the message
protocol is quite trivial (only 4 fields - a sync byte, a sensor id byte,
temperature encoded in a byte and half (3 nibbles), and a checksum byte).
So the code for actually decoding the transmissions is really quite
short.

Most of the device handler code revolves around the debug
outputs, that generate "csv style" lines of output that can be captured
and opened in a spreadsheet package to assist with the reverse
engineering process.  That (debug mode and reverse engineering) is what
the "tutorial" focuses on.  Specifically it covers:

* **how** to batch process sets of captured test data in debug mode to
  produce useful csv files
* a technical aside about the mechanism the "debug to csv" code
  uses to acquire filenames or timestamps to label each line of csv
  output, and
* the strategy I followed to reverse engineer the XC-0324 protocol, or
  in other words **why** I wanted csv files and how I used them.

It is written as if I knew what I was doing - but of course that is far
from the truth, so I have included a section which contains a selection
of some of the references that I found especially useful, ie it contains
pointers to :

* things I wish I had found, read and understood before I started.

PS The tutorial assumes that you :

 * know the basics of git,
 * have successfully compiled a copy of rtl_433,
 * are mildly familiar with shell scripts,
 * can "read" C code, and
 * are comfortable with hex, binary and the idea of how integers of
   various lengths, characters etc are coded into binary values (aka
   sequences of bits - 0's and 1's),

or if not, are happy enough to learn through trial, error and internet
searches.

#### The XC-0324 device protocol

Each XC0324 sensor transmits a pulse every 60 seconds, on frequency of
(approx) 433.920 MHz.

The transmitted temperatures can be seen on a
paired XC0322 monitor.  A single monitor can display the temperatures
from up to 3 XC0324 sensors.

The radio transmissions are OOK (OnOffKeying) and individual bits are
encoded using pulse position modulation (ie the gap width contains the
modulation information about a bits value)

 * Each pulse is about 100*4 us
 * A short gap is (approx) 130*4 us, and represents a 0 bit
 * A long gap is (approx) 250*4 us, and represents a 1 bit.
 
The protocol was deciphered using captured pulses from two sensors.
See the test data in [rtl_433_tests/tests/XC-0324](https://github.com/merbanan/rtl_433_tests/tree/master/tests/XC-0324).

My older sensor transmits a "clean" pulse (ie a captured pulse, examined using
Audacity, has pretty stable I and Q values, ie little phase wandering):

My newer transmitter seems less stable (ie within a single pulse, the I and
Q components of the pulse signal appear to "rotate" through several cycles).
The `rtl_433 -A -D` output correctly guesses the older transmitter modulation
and gap parameters, but mistakes the newer transmitter as pulse width
modulation with "far too many" very short pulses.

A package is 148 bits
  (plus or minus one or two due to demodulation or transmission errors).

Each package contains 3 repeats of the basic 48 bit message,
with 2 zero bits separating each repetition.

A 48 bit message consists of :

 * byte 0 = preamble (for synchronisation), `0x5F`
 * byte 1 = device id
 * byte 2 and the first nibble of byte 3 encode the temperature 
   *    as a 12 bit integer,
   *   transmitted in **least significant bit first** order
   *   in tenths of degree Celsius
   *   offset from -40.0 degrees C (minimum temperature specified for the device)
 * byte 4 is constant (in all my data) `0x80`
   *   *maybe* a battery status ?
 * byte 5 is a check byte (the XOR of bytes 0-4 inclusive)
   *   each bit is effectively a parity bit for correspondingly
      positioned bit in the real message.


#### Batch processing sets of test data files to produce csv file output.

My debugging / deciphering strategy (copied from several helpful blog posts)
involves :

* saving a set of test files (using `rtl_433 -a -t` ) then
* running my 'under development' device handler on the set of test files,
  (repeatedly till I got every thing figured out), using `-D` or `-DD`
  or `-DDD` arguments.

The debug messages from this device handler emit a "debug to csv" format
line. This is essentially a prettily formatted hex and bit pattern version
of (part of) the bitbuffer.

 * `-D` echoes the whole package, row by row.
 * `-DD` echoes one line per decoded message from a package. (ie up to 3 lines
    for the XC-0324)
 * `-DDD` echoes the decoded reference values (temperature and sensor id).

Here are sample extracts of "debug to csv" lines:

From `-D` (the full csv line would usually contain 148 bits, ie
19 data columns)
```
g001_433.92M_250k.cu8, XC0324:D Package, nr[1] r[00] nsyn[00] nc[149] ,at bit [000], 5F 0101 1111, 64 0110 0100, CC 1100 1100, 40 0100 0000, 80 1000 0000, 37 0011 0111, 17 0001 0111,
 ```

From `-DD` (full csv line covers 48 bits, ie 6 data columns)
```
g001_433.92M_250k.cu8, XC0324:DD  Message, nr[1] r[00] nc[149] ,at bit [000], 5F 0101 1111, 64 0110 0100, CC 1100 1100, 40 0100 0000, | 80 1000 0000
```

From `-DDD`
```
g001_433.92M_250k.cu8, XC0324:DDD Reference Values, Temperature 16.3 C, sensor id 64
```

The first column of the spreadsheet contains the test filename (or a
timestamp) responsible for this bitbuffer (more on that below).

The second column of the spreadsheet labels the type of debug output,
and lets `grep` select only those csv lines I want
at a particular point in time.

The third and fourth columns (in `-D` and `-DD` outputs) provide information
about what part of the bitbuffer is on display, and the remaining columns
are data columns from the bitbuffer.

To make use of these debug outputs I have a bash script (called
`exam.sh`), located in the sub-directory where I have stored the test
files.

exam.sh

```
#! /bin/bash
    
for f in g*.cu8;
do
  printf "$f" | ../../src/rtl_433 -R 110 -r $f $1 $2 $3 $4 $5
done
```

* You will need to edit the `../../src/` path to point to your compiled
  version of `rtl_433`.

* `110` is the number rtl_433 currently allocates
 to my prototype device handler.

* `$1 $2 $3 $4 $5` lets me pass up to 5 `rtl_433` arguments through,
  typically that might be `-D`, and/or `-F json`

I use `exam.sh` as follows:

```
./exam.sh -D 2>&1 | grep "XC0324:" > xc0324.csv
```

adjusting

  * `-D`, the debug level, and
  * `"XC0324:"`, the `grep` pattern

appropriately to select what I want in the spreadsheet (`xc0324.csv`).

Then I:

* open `xc0324.csv` in a spreadsheet package (eg Excel),
* manually edit in the correct observed temperatures for each test file,
* sort into observed temperature order, and
* start looking for patterns.

#### Technical Aside : Filename labels in the "debug to csv" output.

Because I use the test filename as a label in several different places
in the debug output from the `xc0324_callback` handler, I chose (in
`exam.sh`) to pipe the test filename to the `stdin` of `rtl_433`, rather
than `echo` it directly to the `stdout` or `stderr` streams.  And in the
callback handler, esssentially I use `fgets(&xc0324_label[0], 48, stdin);`
to acquire the filename for use in the debug statements.

Actually, after I discovered the hard way that `fgets` hangs if
`stdin` is empty, I ended up adding a signal_handler, and an alarm. If
`fgets` hasn't found a label within 2 seconds, it is interrupted, and a
timestamp is generated as an alternative "unique" label for the csv
output lines.

So the callback handler should work whether or not a filename is piped
into via the `stdin` stream.

```
// Get a (filename) label for my debugging output.
// Preferably read from stdin (bodgy but easy to arrange with a pipe)
// but failing that, after waiting 2 seconds, use a local time string

static volatile bool fgets_timeout;

void fgets_timeout_handler(int sigNum) {
    fgets_timeout = 1; // Non zero == True;
}

// Make sure the buffer is well and truly big enough
char xc0324_label[LOCAL_TIME_BUFLEN + 48] = {0};

void get_xc0324_label() {
	// Get a label for this "line" of output read from stdin.
	// fgets has a hissy fit and blocks if nothing there to read!!
	// so set an alarm and provide a default alternative in that case.
  if (strlen(xc0324_label) > 0 ) {
    return; //The label has already been read!
  }
  // Allow fgets 2 seconds to read label from stdin
  fgets_timeout = 0; // False;
  signal(SIGALRM, fgets_timeout_handler);
  alarm(2);
  char * lab = fgets(&xc0324_label[0], 48, stdin);
  xc0324_label[strcspn(xc0324_label, "\n")] = 0; //remove trailing newline
  if (fgets_timeout) {
    // Use a current time string as a default label
    time_t current;
    local_time_str(time(&current), &xc0324_label[0]);
  }
}

```

### A strategy for reverse engineering a device


#### Overview

This is explanation is pretty basic **provided you've done it before**,
but may be helpful if, like I was, you are new to this.

After setting up your own local clone of the rtl_433 repository, and
compiling `rtl_433` from source
(see [rtl_433 : Building.md](https://github.com/merbanan/rtl_433/blob/master/BUILDING.md)),
the gameplan is:

* Find the transmission frequency
* Capture some test files and (**crucial**) record the corresponding
  values from the monitor for **each** file as they are captured.
* Discover the radio encoding protocol that has been used transmit a
  sequence of bits as a radio signal.
* Create a basic prototype callback handler, then
* Iteratively:

    * Batch process the test files to produce csv files, using
      preliminary versions of the device handler,
    * Edit (or cut and paste) the recorded "true" values into the csv
      file,
    * Sort into temperature order,
    * Look for recognisable patterns in the data bits
    * Form a hypothesis about how (part of the message) might be coded
    * Code that into the prototype callback handler,
    * recompile, and
    * iterate to decide whether the hypothesis stands up.
* Then, if you like, submit your test data and your new device handler
  for inclusion in the official rtl_433 repositories.

#### References

Since these are in the nature of "things I wish I had known **before** ..."
I'll include these references here.  It also means I can refer to
them if necessary, rather than repeating information which has already
been covered by others more knowledgeable than me.

They are presented in the order in which they started to become useful
and make sense to me.

1 - "**The Hobbyists Guide to RTL-SDR**" is a book I bought and read to get
started when I bought my first RTL-SDR dongle.  In particular it helped
me get the right drivers installed, and get `SDR#` downloaded and
installed so I could "see" what the dongle was receiving. (A range of
other SDR packages are covered as well)

* [Hobbyists Guide, Kindle Version](https://www.rtl-sdr.com/new-ebook-hobbyists-guide-rtl-sdr/)
or
* [Hobbyists Guide, Amazon Version](https://www.amazon.com/Hobbyists-Guide-RTL-SDR-Software-Defined-ebook/dp/B00KCDF1QI)

2 - The airspy download page for **`SDR#`** - a software defined radio program.
(There are many other good SDR programs, this just happens to be the one I used)

* [SDR# download page](https://airspy.com/download/)

3 - A page from the **rtl_433 wiki**, which is where my current
adventure began :-)

* [rtl_433 Wiki: Adding a new remote device](https://github.com/merbanan/rtl_433/wiki/Adding-a-new-remote-device)

4 - A **blog page (in French)** mentioned on the rtl_433 wiki page.  The wiki
says "a good helper".  I would describe it as an **excellent helper**
(once I had used an internet translation service to cope my appalling
limited French).

NB As at the time of writing (Oct 2018) my browser warns
me that the website's certificate is out of date :-(

But the information and advice certainly isn't

* [An excellent helper, in French](https://enavarro.me/ajouter-un-decodeur-ask-a-rtl_433.html)

5 - The **Audacity** project website. Audacity is great software for looking at
the waveform of individual saved ".cu8" test files. As explained (with
pictures) in the French blog :

* import the .cu8 file as a raw sample,
* nominate "no-endianess",
* set the correct sampling frequency (250000 Hz) to get a valid timescale,
  then
* zoom in and see the individual pulses
* (and even the I and Q components of your signal as the left and right
  stereo channels).

* [Audacity](https://www.audacityteam.org/)

6 - If (like me) you aren't sure what **I and Q components of a signal** means,
there are lots of helpful websites, once you know what to search for. Some
that I particularly liked (the first one has some neat interactive
graphics) are :

* [I/Q Data for Dummies](http://whiteboard.ping.se/SDR/IQ)
* [National Instruments: What s I Q data](http://www.ni.com/tutorial/4805/en/)
* [Understanding I Q signals and quadrature modulation ](https://www.allaboutcircuits.com/textbook/radio-frequency-analysis-design/radio-frequency-demodulation/understanding-i-q-signals-and-quadrature-modulation/)

7 - This sequence of 3 blog posts from **RaysHobby** is a nice practical
worked example of how to collect useful samples and then
reverse engineer / decipher the signals from a temperature sensor.
Initially he used an Arduino, but a Raspberry Pi version is also
covered now.

* [RaysHobbies 1](https://rayshobby.net/wordpress/reverse-engineer-wireless-temperature-humidity-rain-sensors-part-1/)
* [RaysHobbies 2](https://rayshobby.net/wordpress/reverse-engineer-wireless-temperature-humidity-rain-sensors-part-2/)
* [RaysHobbies 3](https://rayshobby.net/wordpress/reverse-engineer-wireless-temperature-humidity-rain-sensors-part-3/)

8 - Last, but far from least, there is a lot that can be learnt from
browsing the **source code of rtl_433 itself**. (There are weblinks
below, but they are also there in your local cloned copy of the
rtl_433 repository!)

* Why not begin with an example that provides a template for a new
  device handler **and** includes **lots** of helpful comments about
  different options you can use?  I certainly wish I'd come across
  this earlier while I was learning about the `rtl_433` code base.
  * [rtl_433 : src/devices/new_template.c](https://github.com/merbanan/rtl_433/blob/master/src/devices/new_template.c)
* I found this following device handler very helpful - perhaps because
  it was so close in concept to the XC-0324 device I was working on
  (simple protocol, reporting temperature with nothing else to confuse
  things)
  * [rtl_433 : src/devices/ambient weather.c](https://github.com/merbanan/rtl_433/blob/master/src/devices/ambient_weather.c)
* And the definitive document about what radio demodulation schemes
  rtl_433 supports, **and** what the key parameters you need to discover
  if your sensor uses that particlar protocol
  * [rtl_433 : include/pulse_demod.h](https://github.com/merbanan/rtl_433/blob/master/include/pulse_demod.h)



#### Reverse engineering a device, step by step advice

###### Finding the transmission frequency.

In this example that was pretty easy (it's written on the back of the
XC0324).  I also checked it using the "waterfall" display from `SDR#`
(a Software Defined Radio program that I had installed from earlier
experiments with my RTL-SDR dongle). The "waterfall" display also
confirmed that the two sensors each transmitted new readings every 60
seconds.

###### Capturing test files.

The mechanics of this are easy as well - `mkdir` and then `cd` to your
chosen test data sub-directory and run `rtl_433 -a -t -f <frequency>`.

Use `rtl_433 -h` to see other arguments you might like to use (and of
course, try `rtl_433 -G -f <frequency>` first - you might well be lucky
and find your device has already has a callback handler).

It is **crucial** to record exactly what value appears on the monitor
when `rtl_433` reports that it is saving a test sample (and which test
file that value belongs to!).  I spent a happy hour or two (spread over
3 or 4 experimental sessions) watching files being saved every 30 seconds
or so, and recording the corresponding values showing on the monitor.
I could have been **much** quicker if I'd known what I needed to get from
each experiment :-)

A "good" set of test data should have:

* some low values (eg put the sensor in the freezer, then take it out
  and record its transmissions as it warms up)
* some high values (eg leave the sensor in full sun, or warm it with a
  hair dryer)
* a run of consecutive values (eg if the monitor reports changes in 0.1
  degrees C steps, several transmission values separated by only 0.1
  degrees.  NB They don't have to be recorded consecutively, just appear
  somewhere in the test data)
* a few "repeat values", and
* if possible, transmissions from more than one sensor (or try removing
  the batteries, and reinserting them - that sometimes makes the sensor
  "choose" a new id).

###### Discovering the radio encoding protocol

The real fun starts here :-)

There are numerous ways of encoding and transmitting digital data via
radio waves, and `rtl_433` has already does the hard work involved in
decoding most of them back from radio waves into bit patterns.  All you
have to do is figure out which transmission protocol the device uses,
and with what parameters.

In fact `rtl_433` even does it's best to guess the transmission
protocol for you - that's what the `-a` option in the capture step does.
Or even better (in my case), later on run your saved test files
through the `-A` option !

But to understand that output, you have to know at least the basics of
how the digital to radio encoding works.

Tip : Now is a good time to read up on I and Q signals, and quadrature
modulation.

Anyway, (with suitable caveats - I'm at the opposite end of the scale to
expert, so this is probably subtly wrong in many places, though
hopefully not too misleading) here goes.

There are two broad ways of superimposing a stream of binary data onto
a radio signal:

  * FSK (Frequency Shift Keying) - think FM radio stations, and
  * OOK (On Off Keying) - think AM radio stations.

"Keying" means "something" changes to signify a bit - think morse code.
With FSK a frequency changes, with OOK an amplitude changes (usually a
"pulse" starts or stops or continues or ...).

This makes more sense if you can "look" at the radio signal - there are
several programs that do exactly that - I used `Audacity`, and,
following the instructions in the (translated) French blog entry,
I imported several of the saved .cu8 test data files into Audacity.

Looking at the waveform from the XC0324 transmissions it pretty clearly
uses OOK.  A transmission consists of a sequence of pulses (ie signal ON), all
of pretty much the same amplitude, separated by gaps (signal OFF).

Tip : Now's a good time to read through `pulse_demod.h`

Within OOK, there are (of course) numerous different ways of
transmitting bits :

* the width of the pulse could signal a bit (eg short pulse = 0, long
  pulse = 1, or vice versa).  This is known as Pulse Width Modulation
  or PWM.
* the width of the gap could signal the bit (eg short gap = 0, long gap
  = 1, or vice versa).  This is known as distance modulation, or Pulse
  Position Modulation or PPM.
* the time until a pulse changes (starts or stops) could signal a bit.
  This is known (I **think**) as Manchester modulation.

There are other schemes as well, but luckily for me, the XC0324 uses a
simple PPM approach (as can be seen by looking at the waveforms in
Audacity and noting that the pulses are more or less the same duration,
but some gaps are long and some gaps are short).  Of course you have to
tell `rtl_433` how short is short, and how long is long.  Duration is
measured in us (microseconds) and can be read (with care) from the
Audacity output (provided you've set the sampling rate parameter
correctly).  Or, now you know what it's talking about, you can also get
these parameters from the `rtl_433 -A` Pulse Analyser output.

```
Registering protocol [110] "XC0324"
Registered 1 out of 110 device decoding protocols
Test mode active. Reading samples from file: g001_433.917M_250k.cu8
Input format: CU8 IQ (2ch uint8)
Detected OOK package	@ @0.000000s
Analyzing pulses...
Total count:  150,  width: 43809		(175.2 ms)
Pulse width distribution:
 [ 0] count:  148,  width:   113 [108;117]	( 452 us)
 [ 1] count:    2,  width:    85 [85;86]	( 340 us)
Gap width distribution:
 [ 0] count:   89,  width:   130 [128;157]	( 520 us)
 [ 1] count:   60,  width:   252 [250;255]	(1008 us)
Pulse period distribution:
 [ 0] count:   89,  width:   244 [218;270]	( 976 us)
 [ 1] count:   60,  width:   366 [364;368]	(1464 us)
Level estimates [high, low]:  15939,     77
Frequency offsets [F1, F2]:   16460,      0	(+62.8 kHz, +0.0 kHz)
Guessing modulation: No clue...
```

Even though the Pulse Analyser says it has no clue about the modulation
(not sure why!) it is easy to see that:

* almost all (148) of the pulses have similar widths (average 113 sample
  units long)
* the gap widths are split between (averages of) 130 and 252, and hence
* the pulse period (gap between successive pulses) is split between
  244 (\~= 113 + 130) and 366 (\~= 113 + 252).

The gap is what varies and hence must carry the information about bits,
so it looks like PPM to me! And the parameters are easy enough to
extract now.

It helps to know that the signal was sampled at 250KHz, so 1
sample unit lasts (1/ 250000) seconds = 4 us (microseconds), so, for
example, a pulse of length 113 sampled units lasts for 113 * 4 us = 452 us
(as reported in the Pulse Analyser output :-) ).

You can read more about the assorted options that `rtl_433` supports,
and how to pass this radio demodulation information to `rtl_433` by
browsing through the comments in `include/pulse_demod.h`.

NB. In the interest of not confusing anyone who it meticulous enough to
check this `-A` output using the test samples in
[rtl_433_tests/tests/XC-0324](https://github.com/merbanan/rtl_433_tests/tree/master/tests/XC-0324).
I'll confess that I have selected an output from my "good" sensor to
show above. The I and Q signal from my "bad" sensor is less stable (frequency
wise), and the `-A` Pulse Analyser output is quite wrong and misleading
for those signals.  One hint that something was wrong for the Analyser
output for that "bad" sensor was that the number of detected bits varied
wildly between samples.  Viewing the waveforms using Audacity confirmed
my suspicions about the "bad" sensor.

###### Writing a prototype callback handler

As explained in the (translated) French blog (and no doubt elsewhere as
well)


> To add a device to rtl_433 the procedure is simple:
>
>* create a `<protocolname>.c` file in `src/devices`,
>* declare the new decoder in `include/rtl_433_devices.h`,
>* add this file to `src/CMakeLists.txt` and `Makefile.am`.
>
>The `src/devices/<protocolname>.c` file will contain two main things:
>
>* the decoding function itself (we will come back to this), and
>* a r_device structure configuring the device and indicating, among other
>  things, the modulation (and therefore the demodulator to use) and a
>  1st level of encoding.

It is also necessary to make a small change to `include/rtl_433.h` as
well.

**`src/devices/xc0324.c`**

You can use `src/devices/newtemplate.c` (changing the filename
appropriately of course) to get you started.  Or browse through the code
for other devices for ideas.

The `src/devices/newtemplates.c` includes a r_device structure appropriate to its
own example device.  For the XC0324, based on the information above,
the device structure looks like:

```
r_device xc0324 = {
    .name           = "XC0324",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 190*4, //(130 + 250)/2  * 4
    .long_limit     = 300*4,
    .reset_limit    = 300*4*2,
    .json_callback  = &xc0324_callback,
};
```

The initial `xc0324_callback` was **very** basic, nothing more than

```
static int xc3024_callback(bitbuffer_t *bitbuffer)
{
    /*
     * Early debugging aid to see demodulated bits in buffer and
     * to determine if your limit settings are matched and firing
     * this callback.
     */
     fprintf(stderr,"xc3024 callback:\n");
     bitbuffer_print(bitbuffer);
}
```

but, once compiled and invoked on my test data set,
it let me see whether my handler was being invoked, and
what `bitbuffer_print` did.

The device handler grew from there as I learnt more about the XC0324
protocol, and about what various parts of the `rtl_433` codebase already
do for you.

Much of the development involved enhancing the debug messages (eventually
arriving at the "debug to csv" approach explained elsewhere), which
made it simpler and quicker to explore the XC0324 protocol itself.

**`include/rtl_433_devices.h`**

It was straightforward to add the definition of the new device handler,
I just inserted an extra line in the file at the obvious place

```
	...
	DECL(wt1024) \
	DECL(xc0324)
	...
```

**`src/CMakeLists.txt` and `Makefile.am`**

Amending these two files was equally simple :

```
	...
	devices/x10_sec.c
	devices/xc0324.c
	...
```
and
```
    ...
    devices/x10_sec.c \
    devices/xc0324.c
    ...
```

**`include/rtl_433.h`**

Finally increase the size of the array that holds all the device 
definitions, to make room for the new device handler

```
...
#define MAX_PROTOCOLS           111 
// ie 110 plus 1 for my prototype xc0324 protocol decoder
...
```


###### Generating the csv files

I've already covered :

* generating csv files from sets of test data,
* opening in a spreadsheet
* editing in the correct temperature value corresponding to each file, and
* sorting into correct value order

so I'll say no more on that here.  Beyond noting that I began with the
`-D` level csv files, and then worked up as I discovered more about the
XC0324 protocol, and embedded that learning into successive versions
of `xc0324_callback`.

###### Looking for patterns in the bits

Everyone has their own way of solving crossword puzzles and Sudoku.

What I *really* did was jump in to the middle, looking for the bytes in
the bitbuffer that seemed to change when temperature changed, partially 
decode that for a few of my (at that stage) 240 tests sample files, and
work outwards from there, trying to figure out what the surrounding
bytes did.

But in retrospect I could have made the job easier with a bit of thought 
beforehand about what to look for, and in what order. So that's what is
reported below :-)

First a bit of thought about what I would be looking for.  Any sensor I
am likely to be using will have to be cheap, and therefore simple. If 
there is an obvious, or easy way of doing something, that's probably
what will have been done.

Keeping power consumption low to extend the battery life will be 
important for the transmitter (and the receiver as well), so the 
transmitter will want to send a message, then go back to sleep till it is
time for the next reading. So it is almost certain to use a broadcast
approach (think UDP if you know about LAN network protocols), rather
than one with a lot of handshaking / conversation back and forth between
the monitor and the sensor (ie **not** like TCP/IP).

If the sensor sleeps between transmissions, it will need to send a wake-up
/ synchronisation call to the receiver when it starts a new transmission 
or message. So there is probably a preamble or sync byte or something similar in the
message, and the simplest place to put it is at the start! So byte 0 
(and perhaps others) will be a constant sync byte / message type 
identifier.

Radio transmissions can suffer from interference, so some form of error 
detection will be essential.  It might be byte by byte - eg 1 bit in
every 8 bit byte could be a parity bit, or it could encompass the whole
message  - eg the last byte - or last few bytes - could be a checksum of
some sort.  Or in more complex cases some form of CRC (cyclic redundancy 
check).  Anyway, expect the message to end with at least one byte for 
error detection purposes.

What to do if an error is detected (since the receiver can't ask for a 
repeat transmission)?  Simplest approach seems to be just send the reading
several times while the sensor is awake and transmitting,
and hope one gets through uncorrupted.  So expect to see several
repeats of the actual temperature reading message in the overall package.

Getting more specific, I know the XC0322 monitor can listen to up to 3
separate XC0324 sensors.  So some form of sensor id must be in the 
message - expect a byte (or at least a nibble) for that.

The XC0324 says it can measure from -40 to +65 degrees Centigrade. And 
it appears to report in 0.1 degree steps.  That amounts to 105*10 
different temperature values to report, which is order of magnitude
2^10. Whatever encoding is used (and my initial guess would be some
form of integer - simplest solution!) 2^10 **must** require more than
8 bits, so expect the temperature field to use 2 bytes
(or maybe 1.5 bytes = 3 nibbles).

Putting all that together, expect a XC0324 message to look like:

* a sync or preamble field (allow 1 byte, probably at the start)
* a sensor id (allow say 1 byte)
* 3 or 4 nibbles of temperature, say 2 bytes
* an error check field (allow 1 byte, probably at the end)

That makes a message of about 5 bytes = 40 bits.

The (good) transmissions in my test data seem to have between 148 and
152 bits, which suggests 3 repeats.  But with quite a few "spare bits"
left over.  In fact it turned out there is an extra byte in the message
(always constant in my observed data so far - perhaps a battery status
indicator?) making 3 message repeats (at 48 bits per message) plus 4
bits left over (a perfect packet turns out to be 148 bits) - so allow
2 bits gap between each repeat message.

OK, now (finally) lets look at some data.

**Find the sync or preamble field, and message length**

The first step is to find the sync byte (should be easy, we expect it to
be constant and at the start of the message). There might be a longer
preamble - but it turns out not to be the case for the XC0324.

`cd` to the test data sub-directory and run

```
./exam.sh -D 2>&1 | grep "XC0324:" > xc0324.D.csv
```

Open the csv file in a spreadsheet package, look at a few lines and
the hex value `0x5F` in byte 0 jumps out as a likely candidate.
(If you are good at binary to hex you can see that the other common
values are what you would get if you prepended a 0 bit to `0x5F`, ie
shifted it right by 1 place in the bitbuffer).

Anyway plug `0x5F` in as the (first version - but it turns out to be right -
of the) sync part of the message, and move on.

I took a guess at 48 for the message length (it seemed to be about right
for packages of 148 (sometimes plus a few) bits; it suggested 3 repeats
and a small number of spacer bits) and luckily it was right.

If I'd guessed too large, the next step would have found only 1 repeat and many
unused bytes at the end of the package; too small a guess and I'd have found the 3
repeats OK, but been left with a few unused bytes after all 3 repeats (which
would have been a strong hint to trial increasing the message length!).

Anyway with those 2 preliminary (but luckily correct) details added to
the developing device handler code it was time to move on to the next
step. Getting the sync byte and message length sorted out first meant
that the prototype decoder could find the start of each repeat message
for me (using the `-DD` outputs) and align each repeat nicely below each
other in the csv file, which makes looking for fields by examining
hex and bit patterns **much** easier.

**Find the sensor id field**

Run (note the increase in debug level, and the altered `grep` filter)
```
./exam.sh -DD 2>&1 | grep "XC0324:DD" > xc0324.DD.csv
```

Open the csv file as a spreadsheet, and examine the hex and bit patterns
again.

First note that with `0x5F` as the sync bit, we are (in most
cases) now finding 3 identical repeats of the message in a single row of
the bitbuffer, nicely aligned in 3 rows of the spreadsheet.

The 3 repeats were far from obvious before, because the 2 bit gap
between the 1st and 2nd repeat of the message made the hex values in the
middle of the message look quite different to the beginning and end.

Since I have data from 2 sensors, the sensor id field should have (only)
two values - and looking at the spreadsheet, byte 1 is the obvious
candidate.

**Find and decipher the temperature field**

The temperature field should change a lot (because there are lots of
different observed temperatures in the test data). byte 2 is an obvious
candidate. (And with the logic of hindsight, as explained above,
probably part of byte 3 as well). The question was, how to extract the
temperature from these bytes?  This is where
editing (or pasting) the true observed temperatures into the rows of the
spreadsheet, then sorting by observed temperature becomes invaluable.
Focussing on those tests data rows where there is a run of consecutive
observed temperatures, and staring at the bit patterns for long enough,
I eventually noticed that the first bit in byte 2 changed (by 1) every
time the temperature increased by 0.1 degrees C.  Eventually I realised
that this suggested something being transmitted least significant bit
first (ie the familiar - once I recognised it - issue of "endianess").
And a bit of searching revealed the `reverse8` function in the `rtl_433`
code set up for just this purpose.  A bit more experimentation and
thought, and the temperature field encoding was solved.

**Decipher byte 4**

Now that the sync byte has been found and the messages are all
nicely aligned to start from the same bit, it is clear that byte 4 is
constant in almost all my observations.  Not much I can deduce about it
(except to speculate it might be battery status?).

**Find and decipher the error checks**

byte 5 is left, at the end of each message, just where I might expect to
find the error check.  (Aside, luckily the XC0324 doesn't seem to use
byte level parity checks, otherwise it would have taken me **much**
longer to spot the least significant bit first pattern for the
temperature field!).

Initially I thought it might be some form of CRC check on the message.
So I perused other devices, did a quick tally to find out which CRC
methods and algorithms seemed to be popular, and tried them out on
the XC0324 message.  All to no avail!

Finally I thought to look at the
pattern of byte 5 from a single sensor as the temperature changed by
0.1 C (so only 1 or 2 bits in the whole message had changed.)  And
noticed that there was a sequential pattern (but with jumps) in the
values of byte 5.
Since my understanding is that CRC are designed to NOT produce
sequential check values like that, I started looking at checksum type
algorithms.

Byte 5 tended to have runs where it decreased as the temperature
increased by 0.1 C (ie 1 or 2 bit change in the temperature field) - but
then it would jump by 8 or 16.  Simple addition of bytes 0 to 4 didn't
work, but after some more gazing at bit patterns, I realised that
"binary addition WITHOUT carry" (aka `XOR`ing bytes 0 to 4) generated
(most of) my byte 5 values.  And when the `XOR`ing didn't work, often
the calculated temperature did not equal the observed one.
Hence the checksum was doing its job, flagging a corrupted message.

Aside - having easy access to the three repeats of a single
message stacked one above the other in the spreadsheet made finding
"bad" checksums easier - if all 3 agreed, the message was probably good!
If 2 agreed and 1 was different, the different one was (probably) a
corrupted message.

I put all that together into my device callback handler, and the job
was done :-)

My final handler (with all the debug code and many of the comments
excluded - if you want to see them, have a look at
`rtl_433/src/devices/xc0324.c`) looks like :

```
/* Decoder for Digitech XC-0324 temperature sensor,
 *  tested with two transmitters.
 */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

#include "bitbuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define MYDEVICE_BITLEN      148
#define MYMESSAGE_BITLEN     48
#define MYMESSAGE_BYTELEN    MYMESSAGE_BITLEN / 8
#define MYDEVICE_STARTBYTE   0x5F
#define MYDEVICE_MINREPEATS  3

static const uint8_t preamble_pattern[1] = {0x5F}; // Only 8 bits

static uint8_t
calculate_paritycheck(uint8_t *buff, int length)
{
    // b[5] is a check byte.
    // Each bit is the parity of the bits in corresponding position of b[0] to b[4]
    // ie b[5] == b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4]
    // and b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4] ^ b[5] == 0x00

    uint8_t paritycheck = 0x00;
    int byteCnt;

    for (byteCnt=0; byteCnt < length; byteCnt++) {
        paritycheck ^= buff[byteCnt];
    }
    // A clean message returns 0x00
    return paritycheck;
}


static int
xc0324_decode(bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos, data_t ** data)
{   // Working buffer
    uint8_t b[MYMESSAGE_BYTELEN];

    // Extracted data values
    int deviceID;
    char id [4] = {0};
    double temperature;
    uint8_t const_byte4_0x80;
    uint8_t parity_check; //parity check == 0x00 for a good message
    char time_str[LOCAL_TIME_BUFLEN];


    // Extract the message
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, MYMESSAGE_BITLEN);

    // Examine the paritycheck and bail out if not OK
    parity_check = calculate_paritycheck(b, 6);
    if (parity_check != 0x00) {
       return 0;
    }

    // Extract the deviceID as int and as hex(arbitrary value?)
    deviceID = b[1];
    snprintf(id, 3, "%02X", b[1]);

    // Decode temperature (b[2]), plus 1st 4 bits b[3], LSB first order!
    // Tenths of degrees C, offset from the minimum possible (-40.0 degrees)
    uint16_t temp = ( (uint16_t)(reverse8(b[3]) & 0x0f) << 8) | reverse8(b[2]) ;
    temperature = (temp / 10.0) - 40.0 ;

    //Unknown byte, constant as 0x80 in all my data
    // ??maybe battery status??
    const_byte4_0x80 = b[4];

    time_t current;
    local_time_str(time(&current), time_str);
    *data = data_make(
            "time",           "Time",         DATA_STRING, time_str,
            "model",          "Device Type",  DATA_STRING, "Digitech XC0324",
            "id",             "ID",           DATA_STRING, id,
            "deviceID",       "Device ID",    DATA_INT,    deviceID,
            "temperature_C",  "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
            "const_0x80",     "Constant ?",   DATA_INT,    const_byte4_0x80,
            "parity_status",  "Parity",       DATA_STRING, parity_check ? "Corrupted" : "OK",
            "mic",            "Integrity",    DATA_STRING, "PARITY",
            NULL);

    return 1;
}

/*
 * List of fields that may appear in the output  for this device
 * when using -F csv.
 */
static char *output_fields[] = {
    "time",
    "model",
    "id",
    "deviceID",
    "temperature_C",
    "const_0x80",
    "parity_status",
    "mic",
    "message_num",
    NULL
};


static int xc0324_callback(bitbuffer_t *bitbuffer)
{
    int r; // a row index
    uint8_t *b; // bits of a row
    unsigned bitpos;
    int result;
    int events = 0;
    data_t * data;

    /*
     * A complete XC0324 package contains 3 repeats of a message in a single row.
     * But there may be transmission or demodulation glitches, and so perhaps
     * the bit buffer could contain multiple rows.
     * So, loop over all rows and check for recognisable messages:
     */
    for (r = 0; r < bitbuffer->num_rows; ++r) {
        b = bitbuffer->bb[r];
        /*
         * Validate message and reject invalid messages as
         * early as possible before attempting to parse data.
         */
        if (bitbuffer->bits_per_row[r] < MYMESSAGE_BITLEN) {
          continue; // to the next row
        }
        // OK, at least we have enough bits
        /*
         * Search for a message preamble followed by enough bits
         * that it could be a complete message:
         */
        bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, r, bitpos,
                (const uint8_t *)&preamble_pattern, 8))
                + MYMESSAGE_BITLEN <= bitbuffer->bits_per_row[r]) {
            events += result = xc0324_decode(bitbuffer, r, bitpos, &data);
            if (result) {
              data_append(data, "message_num",  "Message repeat count", DATA_INT, events, NULL);
              data_acquired_handler(data);
              // Uncomment this `return` to break after first successful message,
              // instead of processing up to 3 repeats of the same message.
              //return events;
            }
            bitpos += MYMESSAGE_BITLEN;
        }
    }
    return events;
}

r_device xc0324 = {
    .name           = "XC0324",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 190*4,// = (130 + 250)/2  * 4
    .long_limit     = 300*4,
    .reset_limit    = 300*4*2,
    .json_callback  = &xc0324_callback,
    .disabled       = 1, // stop my debug output from spamming unsuspecting users
    .fields        = output_fields,
};

```

**Conclusion**

There are definitely cleverer ways of finding the fields and deciphering
their encoding (for eg I've seen reference to using genome sequencing
software to decipher where the fields are - sounds really neat and
certainly something that would be fun to research later).  And more
complex messages (eg more fields, perhaps security and
even encryption issue to consider).

But hopefully this rather long exposition is enough to get you started
on a career as an rtl_433 device reverse engineer.
