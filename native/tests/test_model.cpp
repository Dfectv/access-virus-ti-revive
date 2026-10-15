// RemoteDevice / Conditions on a small hand-written device description (the file format of remotedevice.xml, no
// Access content): conditions, dump fields with masks, aliases and checksums, the update_dump fast path
#include "check.h"
#include "editor/editormodel.h"
#include "model/remotedevice.h"
#include <cstring>

using namespace vti;
using Bytes = std::vector<uint8_t>;

static const char *XML = R"(<?xml version="1.0"?>
<Remotedevice device="Test" version="1">
 <ParameterSets>
  <ParameterSet name="Osc">
   <Parameter name="Shape" minValue="0" maxValue="127" defaultValue="64" part="all"/>
   <Parameter name="Mode" minValue="0" maxValue="3" defaultValue="0" part="all"/>
   <Parameter name="Density" minValue="0" maxValue="127" defaultValue="0" part="all"/>
  </ParameterSet>
  <ParameterSet name="Global">
   <Parameter name="Panel" minValue="0" maxValue="15" defaultValue="0" part="none"/>
  </ParameterSet>
 </ParameterSets>
 <Conditionsets>
  <Condition name="Mode Is Saw"><Test type="equals" param="Osc/Mode" minvalue="1"/></Condition>
  <Condition name="Mode Is Saw"><Test type="equals" param="Osc/Mode" minvalue="3"/></Condition>
  <Condition name="Shape Middle And Mode 0" operand="and">
   <Test type="inside" param="Osc/Shape" minvalue="32" maxvalue="96"/>
   <Test type="equals" param="Osc/Mode" minvalue="0"/>
  </Condition>
  <Condition name="Shape Not Middle" result="invert">
   <Test type="inside" param="Osc/Shape" minvalue="32" maxvalue="96"/>
  </Condition>
  <Condition name="Shape Outside"><Test type="outside" param="Osc/Shape" minvalue="10" maxvalue="20"/></Condition>
  <Condition name="Panel Is 2"><Test type="equals" param="Global/Panel" minvalue="2"/></Condition>
 </Conditionsets>
 <MIDIMessages>
  <MIDIMessage id="Dump" name="Dump">
   <byte index="0" content="byte" tag="240"/>
   <byte index="1" content="Osc/Shape"/>
   <byte index="2" content="Osc/Mode" mask="3" shift="0"/>
   <byte index="2" content="Osc/Density" mask="124" shift="2"/>
   <byte index="3" content="Osc/Shape" part="1"/>
   <byte index="4" content="Osc/Density"/>
   <byte index="5" content="checksum" method="add7" startindex="1" stopindex="4"/>
   <byte index="6" content="byte" tag="247"/>
  </MIDIMessage>
 </MIDIMessages>
</Remotedevice>)";

static RemoteDevice &device()
{
    static RemoteDevice d;
    static bool loaded = false;
    if (!loaded) {
        std::string err;
        Bytes x(XML, XML + strlen(XML));
        loaded = d.load(x, err);
        if (!loaded)
            std::fprintf(stderr, "  load: %s\n", err.c_str());
    }
    return d;
}

TEST(model_load)
{
    RemoteDevice &d = device();
    CHECK(d.find("Osc/Shape") && d.find("Global/Panel"));
    CHECK(!d.find("Osc/Nothing"));
    CHECK(d.message("Dump"));
}

TEST(conditions)
{
    RemoteDevice empty;
    Conditions c(device(), empty);
    std::map<std::string, int> v = {{"Osc/Shape", 64}, {"Osc/Mode", 0}, {"Global/Panel", 2}};
    int seen_part = -99;
    ValueFn f = [&](const std::string &p, int part) {
        if (p == "Global/Panel")
            seen_part = part;
        return v[p];
    };
    CHECK(c.eval("", f, 0));                     // no condition
    CHECK(c.eval("Unknown Condition", f, 0));    // unknown names count as true
    CHECK(!c.eval("Mode Is Saw", f, 0));
    v["Osc/Mode"] = 3;                           // the second alternative
    CHECK(c.eval("Mode Is Saw", f, 0));
    CHECK(!c.eval("Shape Middle And Mode 0", f, 0));
    v["Osc/Mode"] = 0;
    CHECK(c.eval("Shape Middle And Mode 0", f, 0));
    CHECK(!c.eval("Shape Not Middle", f, 0));
    v["Osc/Shape"] = 15;
    CHECK(c.eval("Shape Not Middle", f, 0) && !c.eval("Shape Outside", f, 0));
    CHECK(c.eval("Panel Is 2", f, 5) && seen_part == -1); // part "none": not part specific
    CHECK_EQ(c.default_value("Osc/Shape"), 64);
}

static Bytes blank_dump()
{
    Bytes m = {0xF0, 0, 0, 0, 0, 0, 0xF7};
    device().update_dump(*device().message("Dump"), m, {{"Osc/Shape", 0}, {"x/x", 0}}); // checksum
    return m;
}

TEST(dump_fields_and_checksum)
{
    const RemoteDevice &d = device();
    const MidiMessage &m = *d.message("Dump");
    CHECK_EQ(RemoteDevice::dump_size(m), 7u);
    Bytes msg = blank_dump();
    d.update_dump(m, msg, {{"Osc/Shape", 100}, {"Osc/Mode", 2}, {"Osc/Density", 9}, {"Osc/Shape@1", 7}});
    CHECK_EQ(msg[1], 100);
    CHECK_EQ(msg[2], 2 | 9 << 2); // two fields in one byte
    CHECK_EQ(msg[3], 7);
    CHECK_EQ(msg[4], 9);          // an alias: the same parameter in two places
    CHECK_EQ(msg[5], (100 + msg[2] + 7 + 9) & 0x7F);
    std::map<std::string, int> back;
    std::string err;
    CHECK(d.decode_dump(m, msg, back, err));
    CHECK(back["Osc/Shape"] == 100 && back["Osc/Mode"] == 2 && back["Osc/Density"] == 9 && back["Osc/Shape@1"] == 7);
    msg[5] ^= 1;
    CHECK(!d.decode_dump(m, msg, back, err)); // checksum
    CHECK(!d.decode_dump(m, Bytes(6, 0), back, err)); // length
}

TEST(dump_fast_path_equals_full_path)
{
    const RemoteDevice &d = device();
    const MidiMessage &m = *d.message("Dump");
    for (const char *key : {"Osc/Shape", "Osc/Mode", "Osc/Density", "Osc/Shape@1", "Not/There"})
        for (int v : {0, 1, 3, 31, 64, 127}) {
            Bytes a = blank_dump(), b = a;
            d.update_dump(m, a, {{key, v}});               // one value: the indexed fast path
            d.update_dump(m, b, {{key, v}, {"zz/none", 0}}); // two: the full walk
            CHECK(a == b);
        }
}
