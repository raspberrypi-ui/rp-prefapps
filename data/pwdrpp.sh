#!/bin/bash
export TEXTDOMAIN=rp_prefapps

. gettext.sh

zenity --password --title "$(gettext "Password Required")"

