#!/bin/sh
S=$(dirname "$0"); cd $S
bwrap --dev-bind / / --tmpfs /tmp --bind /tmp/.X11-unix /tmp/.X11-unix --bind /tmp/claude-1000 /tmp/claude-1000 bash -c "cd $S && DISPLAY=:77 timeout 40 ~/virus-ti-native/build/bin/Release/nativehost ~/virus-ti-native/build/VST3/Release/VirusTINative.vst3 --state $1.comp --editor --click 433,24@2 --shot u-$1.ppm@3 --seconds 4" >/dev/null 2>&1
python3 -W ignore -c "
from PIL import Image
o=Image.open('$2').convert('RGB'); u=Image.open('u-$1.ppm').convert('RGB')
for b in ((298,140,378,186),(718,140,798,186)):
    print('diff', sum(1 for x in range(b[0],b[2]) for y in range(b[1],b[3]) if max(abs(a-c) for a,c in zip(o.getpixel((x,y)),u.getpixel((x,y))))>24))"
