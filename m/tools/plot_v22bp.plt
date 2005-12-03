#!/usr/bin/gnuplot -persist

set grid

set xrange [0:4000]

set terminal x11 0
set title "Frequency domain"
set xlabel "Frequency" 1,0
set ylabel "Magnitude"
plot "v22bpf.dat" using 1:2 with lines title "Mag"

set terminal x11 1
set title "Frequency domain"
set xlabel "Frequency" 1,0
set ylabel "Magnitude in dB"
plot "v22bpf.dat" using 1:4 with lines title "Mag"

set terminal x11 2
set title "Frequency domain"
set xlabel "Frequency" 1,0
set ylabel "Phase"
plot "v22bpf.dat" using 1:3 with lines title "Phase"

set xrange [*:*]

set terminal x11 3
set title "Time domain"
set xlabel "Time" 1,0
set ylabel "Amplitude"
plot "v22bpt.dat" u 1:2 with lines notitle

