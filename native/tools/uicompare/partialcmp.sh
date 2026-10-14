#!/bin/bash
# partial vs full repaint per page (no original needed): stress automation until 4 s, screenshot at 6 s, once with
# partial repaints and once with VTI_NATIVE_NO_PARTIAL=1 - the screenshots must be identical. Needs Xvfb :97 and
# base.comp in the current directory (any saved state).
for tab in 244 306 369 432 495 558 622 685 748 810; do
run(){ DISPLAY=:97 VTI_NATIVE_DRAWTIME=1 "$@" timeout 60 ~/virus-ti-native/build/bin/Release/nativehost ~/virus-ti-native/build/VST3/Release/TI\ Revive\ Virus\ Control.vst3 --state base.comp --seconds 7 --stress-params --stress-stop 4 --editor --click $tab,24@0.5 --shot $SHOT@6 2>&1; }
SHOT=p$tab.ppm run env > p$tab.log; SHOT=f$tab.ppm run env VTI_NATIVE_NO_PARTIAL=1 > f$tab.log
echo "tab $tab: full $(grep -c 'editor draw 907x513' p$tab.log)/$(grep -c 'editor draw 907x513' f$tab.log), small $(grep 'editor draw' p$tab.log | grep -vc 907x513): $(cmp -s p$tab.ppm f$tab.ppm && echo IDENTICAL || echo DIFFERENT)"; done
