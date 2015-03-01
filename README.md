## upart

###### A toy for examining partition maps

This will read a hierarchy of partition maps in various formats from a disk or disk image. See the manpage for details on which partition map formats are supported, or just give it a try.

It was initially created as a personal learning tool for partition maps in general, and to have a simple utility which could be built easily on any operating system which I used. I have no great hope that it will be of use to anyone else, but it is here on github for anyone who wants it.

To build and install the binary and a manpage, simply `./configure && make install`. It should build and run correctly on the BSDs and Linux, and has built on MacOS X, Windows, and Haiku in the past. There is incomplete Solaris support as well. Support for partition map formats is platform-independent.
