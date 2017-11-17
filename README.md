# Introduction

Libklvanc is a library which can be used for parsing/generation of Vertical
Ancillary Data (VANC) commonly found in the Serial Digital Interface (SDI) wire protocol.

This repo does NOT contain the library, it contains tools that build against the library.

Users should refer to the "tools" subdirectory for some example applications
which make use of the library.  Note that these tools depend on the Decklink
API headers since they generally interact with a real SDI card.  The library
itself has no dependency on the Decklink API, but the example tools do.

# LICENSE

	LGPL-V2.1
	See the included lgpl-2.1.txt for the complete license agreement.

## Dependencies
* ncurses (optional)
* zlib-dev
* Doxygen (if generation of API documentation is desired)

## Compilation
    ./autogen.sh --build
    ./configure --enable-shared=no
    make

