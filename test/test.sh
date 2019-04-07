#! /bin/bash

. ~/.bashrc

function randval() {
  index=0
  str=""
  for i in {a..z}; do arr[index]=$i; index=`expr ${index} + 1`; done
  for i in {A..Z}; do arr[index]=$i; index=`expr ${index} + 1`; done
  for i in {0..9}; do arr[index]=$i; index=`expr ${index} + 1`; done
  for i in {1..256}; do str="$str${arr[$RANDOM%$index]}"; done
  echo $str
}

val=`randval`

echo "random generate 1000 keys and insert"

i=1000
while ((i>0)); do
	key=`cat /dev/urandom | head -n 10 | md5sum | head -c 16`
	
	echo "$key" >> /tmp/input
	ret=`redis-cli -p 6666 set $key $val`
	if [ $ret != 'OK' ]; then
		echo "$ret"
		exit 1
	fi
	res=`redis-cli -p 6666 get $key`
	if [ $res != $val ]; then
		echo "res=\n$res"
		echo "expect=\n$val"
		echo "\n"
	fi
	let i--
done

echo "begin to test scan"
# sort and compare
redis-cli -p 6666 scan 0000000000000000 zzzzzzzzzzzzzzzz  > /tmp/scan

cat /tmp/input | sort > /tmp/sorted

diff /tmp/scan /tmp/sorted
if [ $? -ne 0 ]; then
	rm /tmp/input
	rm /tmp/scan
	rm /tmp/sorted
	exit 1
fi

rm /tmp/input
rm /tmp/sorted
echo "test put/get/scan passed"

exit 0