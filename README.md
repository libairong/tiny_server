# Tiny Http Server (Now only suport http for static site. Dynamic and https modules will be added in the next few days/months.)

A tiny http server that is especially suitable for low resource devices.

I use it to run my lightweight sites on my VPS and low resource iot board.  
~~It's only 22.4KB after built on my x86 linux and 20.1KB built by poky toolchain for arm cortex a7.~~ (this is v0.0.1, enough for running only one site.)  

Now it's just 43.6 KB on my pc linux with muti-domain matching to multi-sites at one port.

multi-thread, fast and safe enough for general site.  

build:  
`make`

then edit your `host.conf` if you need to run multi-sites with different domains.

usage example:  
`./tiny_server 80  `
