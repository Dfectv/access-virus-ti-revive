# uicompare - pixel comparison of our editor against the original

Needs: Xvfb :77, the original under Wine (`C:\vtihost.exe` + "Virus TI (TI Revive).vst3" in ~/.wine), the Virus on the
Linux broker, `build/` with nativehost and vtimodel. Outputs land next to the scripts (git-ignored - keep them out
of git, they show the original's graphics).

- `st.py` - read/write vtihost states (VTISTATE) and the "MIDI" record container
- `capa.py` - `run(tag, ["Path=value", ...], tab=(x, y))`: base state + changes on part 0's dump, capture the original
- `allpages.py` - all tabs of the base state: original (set ORIG=1) vs ours, % of clearly different pixels
- `fcmp.sh <state> <orig.png>` - render one state with ours (FILTER page) and diff both filter displays

Base state: `VTI_SAVESTATE='C:\base.state' wine 'C:\vtihost.exe' '<Virus TI (TI Revive).vst3>' 25 'C:\out.wav'`.
