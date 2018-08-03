
Yaesutool is a utility for programming handheld radios via a serial or USB
programming cable.  Supported radios:

 * Yaesu FT-60R
 * Yaesu VX-2R

Web site of the project: https://github.com/sergev/yaesutool


## Usage

Save device binary image to file 'device.img', and text configuration
to 'device.conf':

    yaesutool [-v] port

Write image to device.

    yaesutool -w [-v] port file.img

Configure device from text file.
Previous device image saved to 'backup.img':

    yaesutool -c [-v] port file.conf

Show configuration from image file:

    yaesutool file.img

Option -v enables tracing of a serial protocol to the radio:


## Example

Reading configuration of FT-60R handheld:

    C:\> yaesutool -t ft60 COM5
    Radio: Yaesu FT-60R
    Connect to /dev/ttyUSB1 at 9600 baud.
    Read device: please follow the procedure.

    1. Power Off the FT60.
    2. Hold down the MONI switch and Power On the FT60.
    3. Rotate the right DIAL knob to select F8 CLONE.
    4. Briefly press the [F/W] key. The display should go blank then show CLONE.
    5. Press and hold the PTT switch until the radio starts to send.
    -- Or enter ^C to abort the memory read.

    Waiting for data... ############################ done.
    Close device.
    Write image to file 'device.img'.
    Print configuration to file 'device.conf'.

Reading configuration of VX-2R radio:

    C:\> yaesutool -t vx2 COM5
    Radio: Yaesu VX-2
    Connect to /dev/ttyUSB1 at 19200 baud.
    Read device: please follow the procedure.

    1. Power Off the VX-2.
    2. Hold down the F/W key and Power On the VX-2.
       CLONE wil appear on the display.
    3. Press the BAND key until the radio starts to send.
    -- Or enter ^C to abort the memory read.

    Waiting for data... ################################ done.
    Close device.
    Write image to file 'device.img'.
    Print configuration to file 'device.conf'.

## Configurations

You can use these files as examples or templates:
 * ft-60-sunnyvale.conf  - Configurations of my personal handhelds
                           for south SF Bay Area, CA.


## Sources

Sources are distributed freely under the terms of MIT license.
You can download sources via GIT:

    git clone https://github.com/sergev/yaesutool


To build on Linux or Mac OS X, run:

    make
    make install


To build on Windows using MINGW compiler, run:

    gmake -f make-mingw

___
Regards,
Serge Vakulenko
KK6ABQ
