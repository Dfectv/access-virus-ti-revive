# Evidence log

Every conclusion gets an entry. Pointer = capture file + frame number(s), or binary (SHA256 prefix) + function address, or file + line/offset.
Confidence: HIGH (directly observed) / MED (strong inference) / LOW (plausible, needs confirmation).

| ID | Date | Claim | Pointer | Method / command | Confidence |
|----|------|-------|---------|------------------|------------|
| E001 | 2026-10-10 | MSI baseline SHA256 = f9a6f5c929ec894f872052082be9b4abec07e627499549d21c1f362a55e0a466 (47,767,552 B) | originals/Virus TI Software Suite 5.1.7.00 64-bit.msi | sha256sum | HIGH |
| E002 | 2026-10-10 | VM SSH works as desktop-tu4lpnq\<vm user>; device 133e:0815 is passed to VM `win10` (hostdev, guest usb port 4); host sees it on bus 1 | scripts/vm.sh ssh whoami; virsh dumpxml win10; lsusb | Phase 0 checks | HIGH |
