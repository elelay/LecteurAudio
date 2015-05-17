#!/bin/bash

if [ "$1" = "USB" ]
then
	src=.iq
	dst=.usb
elif [ "$1" = "IQ" ]
then
	src=.usb
	dst=.iq
else
	echo "Usage: $0 <USB|IQ>"
	exit -1
fi

if [ $(readlink /etc/asound.conf) = "/etc/asound.conf$dst" ] ; then
	echo "Already using $1"
	exit 0
else
	echo "switching to $1"
fi

for i in /etc/asound.conf /etc/mpd.conf ; do
	unlink $i
	ln -s ${i}${dst} $i
done

mpd --kill
mpd