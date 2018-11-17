#!/bin/bash

if [ -z "$1" ] ; then
  echo input file missing
  echo "Usage: $0 input.cu8 [sample rate in kHz]"
  exit 1
fi
if [ ! -r "$1" ] ; then
  echo input not found
  echo "Usage: $0 input.cu8 [sample rate in kHz]"
  exit 1
fi
file=$1

filename=$(basename "$file")
tempdir=$(mktemp -d)
out="$tempdir/$filename.sr"
trap "rm -f -- '$out'; rmdir -- '$tempdir'" EXIT

if [ -z "$2" ] ; then
  rate=250
else
  rate=$2
fi

if [ ! -z "$3" ] ; then
  echo too many arguments
  echo "Usage: $0 input.cu8 [sample rate in kHz]"
  exit 1
fi

# create channels
rtl_433 -q -s ${rate}k -r "$file" -w F32:I:analog-1-4-1 -w F32:Q:analog-1-5-1 -w F32:AM:analog-1-6-1 -w F32:FM:analog-1-7-1 -w U8:LOGIC:logic-1-1 >/dev/null 2>&1
# create version tag
echo -n "2" >version
# create meta data
cat >metadata <<EOF
[device 1]
capturefile=logic-1
total probes=3
samplerate=$rate kHz
total analog=4
probe1=FRAME
probe2=ASK
probe3=FSK
analog4=I
analog5=Q
analog6=AM
analog7=FM
unitsize=1
EOF

zip "$out" version metadata analog-1-4-1 analog-1-5-1 analog-1-6-1 analog-1-7-1 logic-1-1
rm version metadata analog-1-4-1 analog-1-5-1 analog-1-6-1 analog-1-7-1 logic-1-1

case "$OSTYPE" in
  darwin*)
    open -b org.sigrok.PulseView --fresh --new --wait-apps --args -i "$out"
    ;;
  *)
    pulseview -i "$out"
    ;;
esac

rm -f -- "$out"
rmdir -- "$tempdir"
trap - EXIT
