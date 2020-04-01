# tiny server (now only suport http for static site)

I use it to run my blog site.
It's only 22.4KB after built on my x86 linux and 20.1KB built by poky toolchain for arm cortex a7.

multi-thread, fast and safe enough for general site.

build:
cc tiny_server.c -o tiny_server -lpthread

usage example:
./tiny_server 80