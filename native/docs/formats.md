# Access file formats (own description, from analysis)

The Access plug-in is data-driven. Its files are read at runtime from the user's installation (src/access):
Windows / Wine: PE resources of "Virus Control.dll", types `XML` (7) and `PNG` (438); macOS: files in
"Virus Control.bundle/Contents/Resources" (same names). Nothing of them is stored in this repository.

## remotedevice.xml (ISO-8859-1) - device model, root `<Remotedevice device version releasedate>`

- `ParameterSets/ParameterSet name=G/Parameter` - one parameter, key `G/name`: `minValue maxValue defaultValue`,
  `valuelist` (name of a value list), `part` (`all`, ...), `condition` (name of a condition: visibility/active),
  `defaultmessage` (id of the MIDI message that carries it), `controllerindex`, `shortname`, `displayname`,
  optional `automatable="true"`, `performanceCtrl`. 79 sets, 587 parameters (TI Desktop/Polar/Keyboard, v7.2).
- `Valuelists/Valuelist name type=plain|mapped/Entry index [value]` text = display text of a value; mapped lists
  carry an extra `value` (order/alias).
- `Conditionsets/Condition name operand=or|and result=normal|invert/Test type=inside|equals minvalue maxvalue param`.
  Several `Condition` elements can share a name.
- `MIDIMessages/MIDIMessage id name/byte index type content [tag] [mask shift] [part] [method startindex stopindex]`:
  - `content`: `controller` (0xB0|ch), `polypressure` (0xA0|ch), `sysexstart`/`sysexend`, `byte` (fixed value =
    `tag`, decimal), `deviceid`, `part`, `controllerid` (its `<parameter mapindex content>` children map the
    parameter id inside this message to parameter paths - several paths can share an index), `value`, or a
    parameter path (dumps: one byte per parameter, bit fields via `mask`/`shift`), checksum bytes (`method`).
  - ids: `cc` (page A, 151 parameters), `pp` (page B via poly pressure, 114), `Bank A (70) Single-1` ..
    `Bank G (74) Intern` (parameter SysEx `F0 00 20 33 01 <dev> <page> <part> <id> <value> F7`, pages 0x6E-0x74),
    `VirusC_Singledump` (259 bytes), `VirusTI_Singledump` (447), `VirusTI_MultiDump` (300).
  - verified: `Global Master/Mode` = `F0 00 20 33 01 10 73 00 7A 00 F7` - the original sends exactly this at start.
- `SoftknobMapping/Mapping index parameter` - soft-knob destination list.
- `Initialisation/GlobalInit param value`, `ParamInit param part value` - what the plug-in sets on the device.

## automation.xml - DAW automation ids

`AutomationList/Automation Parameter Part Index`: `Index` IS the original plug-in's VST3 ParamID (part 2 Filter 1
Cutoff = 793, part blocks of 640). 2672 entries; 2671 of them are exactly the original's mapped parameter ids
(listed with vtihost VTI_LISTPARAMS: 10404 parameters, 2703 mapped). The other 32 mapped ids are Aftertouch and
Pitchbend per MIDI channel (IMidiMapping targets, ids 0x100000+ch*0x1000 .. ). -> The rewrite can rebuild the
original's parameter list (and so load projects / automation) from automation.xml + these 32.

Verified with the original (scripted automation through process(), broker trace): ParamID 153 (Filter 1/Cutoff,
part 1) 0.0/0.5/1.0 -> `B0 28 00` / `B0 28 40` / `B0 28 7F` (value = round(norm * 127)); ParamID 128 (Amp
Envelope/Attack) 0.75 -> `B0 3B 5F`. ParamID 156 (Filter 2/Cutoff) 0.25 sent NOTHING (to explain: probably Filters/
Cutoff Link on -> filter 2 follows filter 1). On plug-in close the original resets globals (73 00 3B/3A/3C/7A).

## editor.xml - the UI (VSTGUI-style), root `<Editor>`

- `Prototypes`: 36 control classes with default colours/size - standard (Container, PatchContainer, StaticText,
  Knob/MaskedKnob/Vertical-/Horizontal-/Inverse*Knob, Vertical-/HorizontalSlider, VUSlider, Button, KickButton,
  DblClkButton, URLButton, RadioButton, Inc/DecButton, Menu, HierarchicMenu, TextEdit, TextSlider, NameEdit,
  SearchEdit) and Virus-specific (PatchBrowser, RemoteBrowser, ADSTREnvelope, ArpEdit, OscClassicShape,
  OscHypersawShape, LP4/HP4/BP4/BS4Response filter curves, WebDisplay).
- `Bitmaps/bmpNNNNN Name="X.png" Tile=/TileInsetRect` - 404 bitmap entries (id -> PNG name, tiling insets).
- `Frame/Container ...` - 20 top-level containers (header, page area, right border, pages); every element:
  `Part` (-1 = current part), `Style`, `Tag`, `Name`, `Parameter` (device parameter path, or editor state
  `VirusControl/...`), `NormalBack`/`PressedBack`/`DisabledBack` (PNG names; knob = vertical filmstrip),
  `Condition` (visibility/enable, conditions from remotedevice.xml or editordevice.xml), `Enabled`, `Transparent`,
  text attributes (`Align FontSize FontStyle ControlFont Shadow`), `OnValue` (radio buttons), child `BackColor`,
  `ForeColor` (rgba), `Size left top width height` (relative to the parent).
  Counts: 298 knobs, 158 radio buttons, 109 buttons, 91 menus, 119 containers, 72 static texts, 33 patch containers.
- editordevice.xml (not yet analysed): editor-internal parameters such as `VirusControl/SelectedPage`.

## Still to describe
editor.xml / editordevice.xml (UI), automation.xml (DAW automation ids), the "PR" variants.

## Plug-in state (VST3 component getState; the controller has none) - captured with vtihost VTI_SAVESTATE

Same container as the cache files: "MIDI" + u32 big-endian length of the rest, then records (u32 big-endian length +
one MIDI message). The state IS the list of messages the plug-in sends to the Virus when a project loads:
1. an empty record (length 0),
2. the multi dump `11 00 00` (267 B; the fixed "MultiCtrl!" table, see protocol.md),
3. 16 single dumps `10 00 <part>` (524 B, bank 0 = edit buffer of part 1..16),
4. globals as parameter SysEx (default project: `73 00 2D 02` twice),
5. 224 CCs: controllers 1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16 for channels 1..16 (CC7 = 100, CC8 = 64,
   CC11 = 127, others 0 in the default project).
Default project: 10329 bytes. The rewrite reads and writes exactly this format -> projects saved with the original
load in the rewrite and vice versa.

## VST3 identity and parameter list of the original (vtihost class listing, VTI_LISTPARAMS)
- One single-component class per product (no separate controller class): "Virus TI" B49F90D9735F4305AC5397BE8958CD31,
  "Virus TI Snow" 0B384C1BEADB40679E8EEEE233A98C9C (TI Revive's wrapper: byte 15 XOR 0x5A).
- 10404 parameters; mapped (automatable) = 2671 from automation.xml + 32 MIDI-mapping targets (per channel ch 0..15:
  0x100000 + ch*0x1000 + 0x80 Aftertouch, + 0x81 Pitchbend, default 0.5). Title "[<part>] <shortname>", unit "Part NN/".
  automation.xml is keyed by (parameter, part), later entries win - which is why id 1574 (typo Part="4") is unmapped.
  `vtimodel automation` reproduces all 2671 titles byte-identically.

## A new instance's state (what the native plug-in builds, R3)
- From the user's plug-in binary at runtime: the init single (" -Init-", stored as bank 5 / program 127, format version
  8) and the "MultiCtrl!" multi (AccessFiles::init_single / init_multi; Windows DLL file offsets 0x1c46b0 / 0x1c48c0).
- Parts: init upgraded to version 9 (RemoteDevice::upgrade_single = port of 0x1800a0c00; versions 10-12 only for
  bank-0 dumps), bank 0 / part n, checksums. The original's own default parts differ in 17 legacy bytes (zeroed old
  chorus/delay area etc.) - the sound is the same init patch; the Virus upgrades what it receives to version 12.
- The state's multi = the start-up table with bytes 25..40 and the checksum zeroed (as the original stores it).
- Then `73 00 2D 02` twice (Global Surround/Output Control 2/3 = 2) and the 224 controller defaults.

## editor rendering rules (verified against a screenshot of the original, Easy page 97.0 % identical pixels)
- Knob/slider bitmaps are vertical filmstrips: frames = bitmap height / control height, frame = (v-min)/(max-min).
- RadioButton/Button: NormalBack is a 2-frame strip (0 = off, 1 = selected); PressedBack only while held.
- VUSlider (part strip): NormalBack = 53-frame MIDI activity meter (frame 0 empty .. 52 full; the original's
  "ShowMIDIActivityMeters": per MIDI channel the highest note-on velocity, -1 per editor idle tick; the plug-in's
  fixed multi maps part n to channel n), NormalHandle at (w - handle width) * volume; children of a PatchContainer
  inherit its Part.
- Align="No Label": no text (the bitmap carries it, e.g. CATEGORY, LOAD/SAVE).
- Conditions the original evaluates in code: "VirusControl/Browser/Category Filter (Not) Selected" (filter != 0),
  "Internal System Info/Display Recall Progress" (false outside a recall).
- Category filter: menu = editordevice "Val Name Category Filter" (All + alphabetical); its entry value is the
  category byte stored in the patch (Piano = index 16, stored 18). Search: live, case-insensitive substring of the
  patch name. Like the original (decompile: repository "Search Results", URL search://; user screenshots): search
  text and/or category collect the matching patches of all banks, sorted by name (byte order, duplicates kept), in
  pages of 128 in the UPPER browser ("Browser 0/Selected Bank"; the lower one is "Browser 1"). Title
  "Search Results/" + `Category is "Piano"` | `Name contains "pi"` | `Name contains "pi" and category is "Piano"` +
  ` (page 1 of N)`; the bank arrows page. Sources = device RAM + device ROM + ALL Personal Libraries including
  subfolders (the installer's "Virus TI/TI2/Snow Factory Backup/Rom/*.mid"); copies of one sound count once, key =
  dump bytes 11..264 (page A without its first two bytes). Verified: Piano = 70 entries with exactly the original's
  9 doubles (HalfliteBC, Poly5thmi@, EP+Faze, EP3DosNyt@, CuttaD6, D50-50, Funk-U, SineLang, EP Res).
- Personal Libraries: recursive; folders are submenus in the bank and LOAD/SAVE menus.
- LCD: "VirusControl/Display Original Value" = "[value when editing of this parameter started]"; the category filter
  shows as "Repository Actions Category Filter" / name / [before] / Part n. Cleared, browser 0 returns to its bank. The generic (Linux) menu ticks
  the entry of the menu's value (setValue), not checkEntryAlone.
- Inactive controls (the parameter's own `condition` false, e.g. Chorus Mix with chorus off, Live Mode while the
  sequencer runs; soft knobs without destination) are painted with alpha 0.4.
- Text: bitmap fonts FNT_LCD_5x7_*.png = 96 glyphs (ASCII 32..127) of 6x9 in a vertical strip; StaticText shows its
  Name, TextSlider/Menu the valuelist text (an existing entry wins even if empty: live displays stay blank),
  NameEdit the part's patch name (Single Patch Name/Character 01..10).
- Bitmaps with Tile="true" are nine-part images: TileInsetRect = fixed borders, the middle repeats to the control
  size; text of such controls (menus) is laid out inside the left/right insets.
- Open: the big Filter 1 cutoff ring (MaskedKnob KNB_Cutoff_Rot_Uni) is not drawn by the original at rest; filter
  response displays (LP4/HP4/BP4/BS4Response -> probably KXYControl in the decompile), envelope (KEnvelope), osc
  shapes (KOscClassicShape/KOscHypersawShape), step sequencer (KStepSequencer) are custom drawn.

## ROM banks (firmware.bin) - verified: 26 banks x 128 = 3328 dumps identical to the original's cache
firmware.bin (next to Virus Control.dll / in the Mac Common folder) is an IFF FORM: TABL (chunk names) + chunks
0001..; the device images vti.bin (TI), vti_2.bin (TI2), vti_snow.bin (Snow) are FORMs of 32 KB flash sectors:
35842/35840-byte chunks = 512 records of 70 bytes with 32 data bytes at +3 and +38 (0x8000-byte chunks are raw).
ROM bank i = sectors P(064+2i) (programs 0-63) and P(065+2i) (64-127), 512 bytes per patch = page A + page B; the dump
is header 10 <5+i> <prog>, page A, add7, page B, add7, F7. TI and TI2 ROMs differ -> model from the USB device
descriptor (bcdDevice 0x100 = TI or Snow by product string, 0x300 = TI2). Banks V..Z of the TI2 are "Empty".
- OscHypersawShape (OSC page, HyperSaw mode): three 1-px lines over the element's full height in ForeColor at
  centre and +-w, w = 1.9 + DetuneSpread * 5.6 / 127, centre x + w/2 - 0.25, positions floor(c + off + 0.5);
  density does not change the display (captured from the original for 8 value pairs, all identical).
