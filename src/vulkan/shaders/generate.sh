#!/bin/sh

yourfilenames=`ls ./*.shader`
for eachfile in $yourfilenames
do
   xxd -i "$eachfile" ../vkshaders/"$eachfile".h
done

echo "finish generating shaders"