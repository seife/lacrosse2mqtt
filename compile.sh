#!/bin/bash

IAM=${0##*/}
BOARD_REV=""
case $IAM in
    compile.sh|upload.sh)
        IAM=${IAM%.sh}
        BOARD_NAME=ttgo-lora32
        BOARD_REV=:Revision=TTGO_LoRa32_V1
        ;;
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
	PARAM=(--build-property "build.extra_flags.esp32=-DLACROSSE2MQTT_VERSION=\"$MYVERSION\"")
	# allow to override dependencies in the "deps" subfolder
	# --libraries does not work good enough (still random priority), so list all explicitly
	for dep in deps/*; do
		[ "$dep" = "deps/*" ] && break
		PARAM+=(--library "$dep")
	done
	PARAM+=(--warnings all)
fi

arduino-cli "$IAM" -b esp32:esp32:${BOARD_NAME}${BOARD_REV} "${PARAM[@]}" "$@"
