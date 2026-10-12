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

## Still to describe
editor.xml / editordevice.xml (UI), automation.xml (DAW automation ids), the "PR" variants.
