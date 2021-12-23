#!/bin/bash

IAM=${0##*/}
IAM=${IAM%.sh}

PARAM=()
if [ "$IAM" = upload ]; then
	if [ -z "$1" ]; then
		echo "please give upload port!"
		exit 1
	fi
	PARAM=(-v -p "$1")
	shift
fi
arduino-cli "$IAM" -b esp32:esp32:ttgo-lora32-v1 "${PARAM[@]}" $@
