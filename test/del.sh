#! /bin/bash

. ~/.bashrc

keysfile=/tmp/scan

if [ ! -f $keysfile ]; then
	echo "file $keysfile does not exist, exit"
	exit 1
fi

for key in `cat $keysfile`
do
	res=`redis-cli -p 6666 delete $key`
	if [ $res != '1' ]; then
		echo "delete $res failed"
		rm $keysfile
		exit 1
	fi
done

rm $keysfile
echo "test delete and info passed"
exit 0
