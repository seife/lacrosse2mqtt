#!/bin/bash

IAM=${0##*/}
case $IAM in
    *-heltec-V2.sh)
        IAM=${IAM%-heltec-V2.sh}
        BOARD_NAME=heltec_wifi_lora_32_V2
        ;;
    *-heltec-V3.sh)
        IAM=${IAM%-heltec-V3.sh}
        BOARD_NAME=heltec_wifi_lora_32_V3
        ;;
    *)  echo "invalid script name $IAM"
        exit 1 ;;
esac


MYVERSION=$(git describe --always --dirty)
PARAM=()
if [ "$IAM" = upload ]; then
	if [ -z "$1" ]; then
		echo "please give upload port! or hostname"
		exit 1
	fi
	if ! [[ "$1" =~ "/dev/"* ]]; then
		curl -v -F "image=@build/esp32.esp32.$BOARD_NAME/lacrosse2mqtt.ino.bin" \
			-H "Origin: http://$1" \
			"$1"/update
		echo
		exit
	fi
	PARAM=(-v -p "$1")
	shift
else
	# PARAM=(--build-property "build.defines=-DLACROSSE2MQTT_VERSION=\"$MYVERSION\"") # pre esp32-arduino 2.0
	PARAM=(--build-property "build.extra_flags.esp32=-DARDUINO_USB_CDC_ON_BOOT=0 -DLACROSSE2MQTT_VERSION=\"$MYVERSION\"")
	PARAM+=(--warnings all)
fi

arduino-cli "$IAM" -b esp32:esp32:$BOARD_NAME "${PARAM[@]}" $@
