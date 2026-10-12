# virus-ti-native (private)

Native rewrite of the Access Virus TI plug-in - stage 3 of TI Revive. Plan: [docs/plan.md](docs/plan.md),
progress: [docs/status.md](docs/status.md).

Rules: no Access material in git (graphics, XML, firmware, decompiled code) - the plug-in loads the skin and device
description at runtime from the user's own Access installation; device recordings stay in work/.

Build (Linux): `git submodule update --init external/ti-revive && (cd external/vst3sdk && git submodule update --init base cmake pluginterfaces public.sdk vstgui4) && cmake -S . -B build -G Ninja && cmake --build build`
