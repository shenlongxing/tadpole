#! /bin/bash

# clear test data
function clear() {
	dir=`grep ^dir ../tadpole.conf | awk -F ' ' '{print $2}'`
	datafile=`grep ^dbfilename ../tadpole.conf | awk -F ' ' '{print $2}'`
	> $dir/$datafile
}

. ~/.bashrc
which redis-cli > /dev/null 2>&1
if [ $? -ne 0 ]; then
	"Redis client does not configured correctly, exit"
	exit 1
fi

# start up process
../tadpole -c ../tadpole.conf

# check status
ps -fe | grep [t]adpole > /dev/null 2>&1
if [ $? -ne 0 ]; then
	echo "startup process failed, exit"
	exit 1
fi

# run test script
res=`sh test.sh`
if [ $? -ne 0 ]; then
	echo "$res"
	redis-cli -p 6666 shutdown
	clear
	exit 1
fi
echo "$res"

res=`sh del.sh`
if [ $? -ne 0 ]; then
	echo "$res"
	redis-cli -p 6666 shutdown
	clear
	exit 1
fi
echo "$res"


# shutdown process
redis-cli -p 6666 shutdown
clear

echo "All test passed"


exit 0
