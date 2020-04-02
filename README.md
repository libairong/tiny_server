# tiny server (now only suport http for static site)

I use it to run my blog site.  
~~It's only 22.4KB after built on my x86 linux and 20.1KB built by poky toolchain for arm cortex a7.~~ (this is v0.0.1 without muti-domain matching to multi-sites at one port, but enough for run a one site.)  

Now it's just 43.6 KB on my pc linux with muti-domain matching to multi-sites at one port.

multi-thread, fast and safe enough for general site.  

build:  
`make`

then edit your `host.conf` if you need to run multi-sites with different domains.

usage example:  
`./tiny_server 80  `
