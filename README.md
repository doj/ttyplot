# ttyplot
a realtime plotting utility for text mode consoles and terminals with data input from stdin / pipe.

This version is forked from https://github.com/tenox7/ttyplot

Reads data from standard input / unix pipe, most commonly some tool like *ping, snmpget, netstat, ip link, ifconfig, sar, vmstat*, etc. and plots in text mode on a terminal in real time, for example a simple **ping**:

![ttyplot ping](ttyplot-ping.png)

Supports rate calculation for counters and up to two graphs on a single display using reverse video for second line, for example **snmpget**, **ip link**, **rrdtool**, etc:

![ttyplot snmp](ttyplot-snmp.png)

## examples

### Linux: cpu usage from vmstat using awk to pick the right column
```sh
vmstat -n 1 | gawk '{ print 100-int($(NF-2)); fflush(); }' | ttyplot
```

### Linux: cpu usage from sar with title and fixed scale to 100%
```sh
sar 1 | gawk '{ print 100-int($NF); fflush(); }' | ttyplot -s 100 -t "cpu usage" -u "%" -b -c '|'
```

### Linux: memory usage from sar, using perl to pick the right column
```sh
sar -r 1 | perl -lane 'BEGIN{$|=1} print "@F[5]"' | ttyplot -s 100 -t "memory used %" -u "%"
```

### OsX: memory usage
```sh
vm_stat 1 | perl -e '$|=1;<>;<>;while(<>){@_=split(/\s+/);print " ".($_[2]*4096/1024/1024/1024)}' | ttyplot -M 0 -t "MacOS Memory Usage" -u GiB -b
```

### Linux: number of processes in running and io blocked state
```sh
vmstat -n 1 | perl -lane 'BEGIN{$|=1} print "@F[0,1]"' | ttyplot -2 -t "procs in R and D state"
```

### Linux: load average via uptime and awk
```sh
{ while true; do uptime | gawk '{ gsub(/,/, ""); print $(NF-2) }'; sleep 1; done } | ttyplot -t "load average" -s load
```

### Linux ping plot
```sh
ping 8.8.8.8 | sed -u 's/^.*time=//g; s/ ms//g' | ttyplot -t "ping to 8.8.8.8" -u ms -b
```

### OsX ping plot
```sh
ping 8.8.8.8 | sed -l 's/^.*time=//g; s/ ms//g' | ttyplot -t "ping to 8.8.8.8" -u ms -b
```

### Linux: wifi signal level in -dBM (higher is worse) using iwconfig
```sh
{ while true; do iwconfig 2>/dev/null | grep "Signal level" | sed -u 's/^.*Signal level=-//g; s/dBm//g'; sleep 1; done } | ttyplot -t "wifi signal" -u "-dBm" -s 90
```

### OsX: wifi signal
```sh
{ while true; do /System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport --getinfo | awk '/agrCtlRSSI/ {print -$2; fflush();}'; sleep 1; done } | ttyplot -t "wifi signal" -u "-dBm" -s 90
```

### Linux: cpu temperature from proc
```sh
{ while true; do awk '{ printf("%.1f\n", $1/1000) }' /sys/class/thermal/thermal_zone0/temp; sleep 1; done } | ttyplot -t "cpu temp" -u C
```

### Linux: fan speed from lm-sensors using grep, tr and cut
```sh
{ while true; do sensors | grep fan1: | tr -s " " | cut -d" " -f2; sleep 1; done } | ttyplot -t "fan speed" -u RPM
```

### memory usage from rrdtool and collectd using awk
```sh
{ while true; do rrdtool lastupdate /var/lib/collectd/rrd/$(hostname)/memory/memory-used.rrd | awk 'END { print ($NF)/1024/1024 }'; sleep 1; done } | ttyplot -m $(awk '/MemTotal/ { print ($2)/1024 }' /proc/meminfo) -t "Memoru Used" -u MB
```

### bitcoin price chart using curl and jq
```sh
{ while true; do curl -sL https://api.coindesk.com/v1/bpi/currentprice.json | jq .bpi.USD.rate_float; sleep 600; done } | ttyplot -t "bitcoin price" -u usd
```

### stock quote chart
```sh
{ while true; do curl -sL https://api.iextrading.com/1.0/stock/googl/price; echo; sleep 600; done } | ttyplot -t "google stock price" -u usd
```

### prometheus load average via node_exporter
```sh
{ while true; do curl -s http://10.4.7.180:9100/metrics | grep "^node_load1 " | cut -d" " -f2; sleep 1; done } | ttyplot
```

## network/disk throughput examples
ttyplot supports "two line" plot for in/out or read/write.

### snmp network throughput for an interface using snmpdelta
```sh
snmpdelta -v 2c -c public -Cp 10 10.23.73.254 1.3.6.1.2.1.2.2.1.{10,16}.9 | gawk '{ print $NF/1000/1000/10; fflush(); }' | ttyplot -2 -t "interface 9 throughput" -u Mb/s
```

### local network throughput for all interfaces combined from sar
```sh
sar -n DEV 1 | gawk '{ if($6 ~ /rxkB/) { print iin/1000; print out/1000; iin=0; out=0; fflush(); } iin=iin+$6; out=out+$7; }' | ttyplot -2 -u "MB/s"
```

### disk throughput from iostat
```sh
iostat -xmy 1 nvme0n1 | stdbuf -o0 tr -s " " | stdbuf -o0 cut -d " " -f 4,5 | ttyplot -2 -t "nvme0n1 throughput" -u MB/s
```

## rate calculator for counters
ttyplot also supports *counter* style metrics, calculating *rate* by measured time difference between samples.

### snmp network throughput for an interface using snmpget
```sh
{ while true; do snmpget -v 2c -c public 10.23.73.254 1.3.6.1.2.1.2.2.1.{10,16}.9 | awk '{ print $NF/1000/1000; }'; sleep 10; done } | ttyplot -2 -r -u "MB/s"
```

### local interface throughput using ip link and jq
```sh
{ while true; do ip -s -j link show enp0s31f6 | jq .[].stats64.rx.bytes/1024/1024,.[].stats64.tx.bytes/1024/1024; sleep 1; done } | ttyplot -r -2 -u "MB/s"
```

### prometheus node exporter disk throughput for /dev/sda
```sh
{ while true; do curl -s http://10.11.0.173:9100/metrics | awk '/^node_disk_.+_bytes_total{device="sda"}/ { printf("%f\n", $2/1024/1024); }'; sleep 1; done } | ttyplot -r -2 -u MB/s -t "10.11.0.173 sda writes"
```

### network throughput from collectd with rrdtool and awk
```sh
{ while true; do rrdtool lastupdate /var/lib/collectd/rrd/$(hostname)/interface-enp1s0/if_octets.rrd | awk 'END { print ($2)/1000/1000, ($3)/1000/1000 }'; sleep 10; done } | ttyplot -2 -r -t "enp1s0 throughput" -u MB/s
```

## command line arguments

```
  ttyplot [-2] [-k] [-r] [-b] [-c char] [-e char] [-E char] [-s scale] [-S scale] [-m max] [-M min] [-t title] [-u unit] [-C 'col1 col2 ...']
  -2 read two values and draw two plots
  -k key/value mode
  -r rate of a counter (divide value by measured sample interval)
  -b draw bar charts
  -c character(s) for the graph, not used with key/value mode
  -e character to use for error line when value exceeds hardmax, default: 'e'
  -E character to use for error symbol displayed when value is less than hardmin, default: 'v'
  -s initial positive scale of the plot (can go above if data input has larger value)
  -S initial negative scale of the plot
  -m maximum value, if exceeded draws error line (see -e), upper-limit of plot scale is fixed
  -M minimum value, if entered less than this, draws error symbol (see -E), lower-limit of the plot scale is fixed
  -t title of the plot
  -u unit displayed beside vertical bar
  -C set list of colors: black,blk,bk  red,rd  green,grn,gr  yellow,yel,yl  blue,blu,bl  magenta,mag,mg  cyan,cya,cy,cn  white,wht,wh
```

## data input

By default ttyplot reads double values from STDIN.
Every value is plotted on the screen.

If the -2 mode is enabled, 2 double values are read from STDIN for each update of the 2 graphs.

If the -k mode is enabled, ttyplot reads any number of key/value pairs from STDIN.
The key is a string without whitespace, the value is a double.
The key/value pairs are separated by whitespace.
After reading a newline the graphs are updated.

See the [test.pl](https://github.com/doj/ttyplot/blob/master/test.pl) program for examples how to produce input for ttyplot.

## frequently questioned answers
### How to disable stdio buffering?
In unix by default stdio is buffered. This can be disabled [various ways](http://www.perkin.org.uk/posts/how-to-fix-stdio-buffering.html) or read [Output buffering](https://collectd.org/wiki/index.php/Plugin:Exec#Output_buffering).

### ttyplot quits when there is no more data
It's by design, you can work around by adding `sleep`, `read`, `cat`, etc:

```sh
{ echo 1 2 3; cat; } | ttyplot
```

### ttyplot erases screen when exiting
This is because of [alternate screen](https://invisible-island.net/xterm/xterm.faq.html#xterm_tite) in xterm-ish terminals; if you use one of these this will likely work around it:

```sh
echo 1 2 3 | TERM=vt100 ttyplot
```

you can also permanently fix the terminfo entry (this will make a copy in ~/.terminfo/):

```sh
infocmp -I $TERM | sed -e 's/smcup=[^,]*,//g' -e 's/rmcup=[^,]*,//g' | tic -
```

### when running interactively and non-numeric data is entered (eg. some key) ttyplot hangs
press `ctrl^j` to re-set

## bugs and future features

See the [TODO.md](https://github.com/doj/ttyplot/blob/master/TODO.md) file.

## legal stuff
```
License: Apache 2.0
Copyright (c) 2013-2018 Antoni Sawicki
Copyright (c) 2019-2021 Google LLC
Copyright (c) 2022 by Dirk Jagdmann <doj@cubic.org>
```
