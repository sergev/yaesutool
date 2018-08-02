
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

For example:

    C:\> yaesutool.exe -t ft60 COM5
    Connect to COM5 at 9600.
    Radio: Yaesu FT-60
    Read device: ################################################## done.
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
