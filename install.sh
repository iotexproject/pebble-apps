
# # Environment variables
red=`tput setaf 1`
# purple=`tput setaf 93`
# blue=`tput setaf 21`
green=`tput setaf 2`
gold=`tput setaf 214`
reset=`tput sgr0`

. ./build/setenv.sh

git submodule update --init --recursive

read -p "Which ${green}IoTeX${reset} ${gold}Pebble Tracker Firmware${reset} do you want to build? \n
        (${red}1${reset}): Aries
        (${red}2${reset}): Asset Tracker
        " FIRMWARE_SELECTION_CONFIG

FIRMWARE_SELECTION=$(case "$FIRMWARE_SELECTION_CONFIG" in
  (1)    echo "aries"           ;;
  (2)    echo "asset_tracker"   ;;
esac)

case $FIRMWARE_SELECTION in
  aries)
    FIRMWARE_PATH="/app/pebble-firmware-legacy/nrf/applications/Aries/" ;;

  asset_tracker)
    FIRMWARE_PATH="/app/pebble-firmware-legacy/nrf/applications/asset_tracker/" ;;

  *)
    echo "${red}Not a valid answer. Terminating${reset}"
    exit 0 ;;
esac

echo "This script will attempt to install ${green}$BUILD $FIRMWARE_PATH ${reset}"

# Getting necessary user input
read -p "Do you want to perform a first-time installation (${red}F${reset}) or run the application (${green}R${reset})?: " MODE
MODE_flag=$(case "$MODE" in
  (F|f)
    echo "docker-compose -p JRPC-pebble-firmware -f ./build/docker-compose.yaml up --no-deps --build" ;;
  (R|r)
    echo "docker-compose -p JRPC-pebble-firmware -f ./build/docker-compose.yaml up" ;;
esac)

$MODE_flag



