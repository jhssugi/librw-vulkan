#!/bin/sh

yourfilenames=`ls ./*.shader`
for eachfile in $yourfilenames
do
   xxd -i "$eachfile" "$eachfile".h
done

echo "finish generating shaders"