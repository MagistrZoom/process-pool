#!/usr/bin/ksh

i=0
while [ $i -lt 1024 ]
do
  ./client localhost / > out/$i &
  i=$((i+1))
done
