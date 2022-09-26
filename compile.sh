#!/bin/bash

IAM=${0##*/}
IAM=${IAM%.sh}

MYVERSION=$(git describe --always --dirty)
PARAM=()
if [ "$IAM" = upload ]; then
	if [ -z "$1" ]; then
		echo "please give upload port! or hostname"
		exit 1
	fi
	if ! [[ "$1" =~ "/dev/"* ]]; then
		curl -v -F "image=@build/esp32.esp32.ttgo-lora32/lacrosse2mqtt.ino.bin" "$1"/update
		echo
		exit
	fi
	PARAM=(-v -p "$1")
	shift
else
	# PARAM=(--build-property "build.defines=-DLACROSSE2MQTT_VERSION=\"$MYVERSION\"") # pre esp32-arduino 2.0
	PARAM=(--build-property "build.extra_flags.esp32=-DARDUINO_USB_CDC_ON_BOOT=0 -DLACROSSE2MQTT_VERSION=\"$MYVERSION\"")
fi

arduino-cli "$IAM" -b esp32:esp32:ttgo-lora32:Revision=TTGO_LoRa32_V1 "${PARAM[@]}" $@
