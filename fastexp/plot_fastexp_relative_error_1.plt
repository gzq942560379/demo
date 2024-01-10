set term pngcairo font "Times-New-Roman,20" size 800,600
set output "./fastexp_relative_error_1.png"

set xlabel "x"
set ylabel "diff"
set title "fastexp diff"


set xrange [0:]

plot \
'./fastexp_1.dat' using 1:5 lw 1 with linespoints title "fastexp_1", \



