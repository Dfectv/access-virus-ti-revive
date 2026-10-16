// RemoteDevice's parameter messages on a small hand-written description (the remotedevice.xml format, invented
// content - no Access data): controller, poly pressure and the 11-byte parameter SysEx, encode <-> decode, values from
// valuelist texts, the start-up messages (GlobalInit / ParamInit) and the automation list. Runs in CI.
#include "check.h"
#include "model/remotedevice.h"
#include <cstring>
using namespace vti;
using Bytes = std::vector<uint8_t>;

static const char *XML = R"(<?xml version="1.0"?>
<Remotedevice device="Proto" version="1">
 <ParameterSets>
  <ParameterSet name="Osc">
   <Parameter name="Shape" minValue="0" maxValue="127" defaultValue="64" part="all" defaultmessage="cc" controllerindex="17" shortname="Osc Shape"/>
   <Parameter name="Wave" minValue="0" maxValue="3" defaultValue="0" valuelist="Waves" part="all" defaultmessage="cc" controllerindex="18"/>
  </ParameterSet>
  <ParameterSet name="Fx">
   <Parameter name="Mix" minValue="0" maxValue="127" defaultValue="0" part="all" defaultmessage="pp" controllerindex="85"/>
   <Parameter name="Mix Alias" minValue="0" maxValue="127" defaultValue="0" part="all" defaultmessage="pp" controllerindex="85"/>
  </ParameterSet>
  <ParameterSet name="Global">
   <Parameter name="Channel" minValue="0" maxValue="15" defaultValue="0" valuelist="Channels" part="none" defaultmessage="Page G" controllerindex="9"/>
   <Parameter name="Mode" minValue="0" maxValue="2" defaultValue="0" valuelist="Modes" part="none" defaultmessage="Page G" controllerindex="10"/>
   <Parameter name="Unsent" minValue="0" maxValue="1" defaultValue="0" part="none"/>
  </ParameterSet>
 </ParameterSets>
 <Valuelists>
  <Valuelist name="Waves" type="plain"><Entry index="0">Sine</Entry><Entry index="1">Saw</Entry></Valuelist>
  <Valuelist name="Channels" type="plain"><Entry index="0">01</Entry><Entry index="1">02</Entry></Valuelist>
  <Valuelist name="Modes" type="plain"><Entry index="0">Single</Entry><Entry index="1">Multi</Entry></Valuelist>
 </Valuelists>
 <MIDIMessages>
  <MIDIMessage id="cc" name="cc">
   <byte index="0" content="controller"/>
   <byte index="1" content="controllerid"><parameter mapindex="17" content="Osc/Shape"/><parameter mapindex="18" content="Osc/Wave"/></byte>
   <byte index="2" content="value"/>
  </MIDIMessage>
  <MIDIMessage id="pp" name="pp">
   <byte index="0" content="polypressure"/>
   <byte index="1" content="controllerid"><parameter mapindex="85" content="Fx/Mix"/></byte>
   <byte index="2" content="value"/>
  </MIDIMessage>
  <MIDIMessage id="Page G" name="Page G">
   <byte index="0" content="sysexstart"/>
   <byte index="1" content="byte" tag="0"/>
   <byte index="2" content="byte" tag="32"/>
   <byte index="3" content="byte" tag="51"/>
   <byte index="4" content="byte" tag="1"/>
   <byte index="5" content="deviceid"/>
   <byte index="6" content="byte" tag="115"/>
   <byte index="7" content="part"/>
   <byte index="8" content="controllerid"><parameter mapindex="9" content="Global/Channel"/><parameter mapindex="10" content="Global/Mode"/></byte>
   <byte index="9" content="value"/>
   <byte index="10" content="sysexend"/>
  </MIDIMessage>
 </MIDIMessages>
 <Initialisation>
  <GlobalInit param="Global/Mode" value="Multi"/>
  <GlobalInit param="Global/Channel" value="02"/>
  <GlobalInit param="Global/Unsent" value="1"/>
  <ParamInit param="Osc/Wave" value="Saw" part="3"/>
  <ParamInit param="Osc/Shape" value="Nonsense" part="1"/>
 </Initialisation>
</Remotedevice>)";

static RemoteDevice &dev()
{
    static RemoteDevice d;
    static bool loaded = false;
    if (!loaded) {
        std::string err;
        loaded = d.load(Bytes(XML, XML + strlen(XML)), err);
        CHECK(loaded);
    }
    return d;
}

TEST(protocol_encode_three_transports)
{
    RemoteDevice &d = dev();
    CHECK(d.encode("Osc/Shape", 100, 2) == (Bytes{0xB2, 17, 100}));    // controller on the part's channel
    CHECK(d.encode("Osc/Shape", 200, 0) == (Bytes{0xB0, 17, 200 & 0x7F})); // 7 bits
    CHECK(d.encode("Fx/Mix", 127, 15) == (Bytes{0xAF, 85, 127}));      // poly pressure
    CHECK(d.encode("Global/Mode", 1, 0) == (Bytes{0xF0, 0, 32, 51, 1, 0x10, 115, 0, 10, 1, 0xF7})); // parameter SysEx
    CHECK(d.encode("Global/Mode", 1, 0, 0x7F)[5] == 0x7F); // the device id
    CHECK(d.encode("Fx/Mix Alias", 5, 0).empty()); // not mapped in its message's controllerid byte
    CHECK(d.encode("Global/Unsent", 1, 0).empty() && d.encode("No/Such", 1, 0).empty());
    uint8_t buf[2];
    CHECK(d.encode_into("Global/Mode", 1, 0, buf, sizeof buf) == 0); // too small a buffer
}

TEST(protocol_encode_into_matches_encode)
{
    // encode_into is the automation path (no allocation): it must give the same bytes as encode
    RemoteDevice &d = dev();
    uint8_t buf[16];
    struct Case {
        const char *path;
        int value, part, device_id;
    } cases[] = {{"Osc/Shape", 100, 2, 0x10}, {"Osc/Wave", 200, 15, 0x10}, {"Fx/Mix", 127, 15, 0x10},
                 {"Global/Mode", 1, 0, 0x10}, {"Global/Channel", 1, 0, 0x7F}};
    for (auto &c : cases) {
        Bytes want = d.encode(c.path, c.value, c.part, c.device_id);
        size_t n = d.encode_into(c.path, c.value, c.part, buf, sizeof buf, c.device_id);
        CHECK(!want.empty() && Bytes(buf, buf + n) == want);
    }
    CHECK(d.encode_into("Fx/Mix Alias", 5, 0, buf, sizeof buf) == 0); // not mapped in its message
    CHECK(d.encode_into("Global/Unsent", 1, 0, buf, sizeof buf) == 0); // no message
    CHECK(d.encode_into("No/Such", 1, 0, buf, sizeof buf) == 0);
}

TEST(protocol_decode_back)
{
    RemoteDevice &d = dev();
    auto c = d.decode({0xB2, 17, 100});
    CHECK(c.size() == 1 && c[0].path == "Osc/Shape" && c[0].part == 2 && c[0].value == 100);
    auto pp = d.decode({0xA5, 85, 7}); // both parameters on poly pressure 85 come back
    CHECK(pp.size() == 2 && pp[0].part == 5 && pp[0].value == 7);
    auto sx = d.decode({0xF0, 0, 32, 51, 1, 0x10, 115, 0, 10, 2, 0xF7});
    CHECK(sx.size() == 1 && sx[0].path == "Global/Mode" && sx[0].value == 2);
    CHECK(d.decode({0xB0, 99, 1}).empty());                                     // unknown controller
    CHECK(d.decode({0x90, 60, 100}).empty());                                   // a note
    CHECK(d.decode({0xF0, 0, 32, 51, 1, 0x10, 0x55, 0, 10, 2, 0xF7}).empty()); // an unknown page
    // every encodable parameter decodes to itself
    for (const char *p : {"Osc/Shape", "Osc/Wave", "Fx/Mix", "Global/Channel", "Global/Mode"}) {
        auto m = d.encode(p, 1, 0);
        bool found = false;
        for (auto &x : d.decode(m))
            found |= x.path == p && x.value == 1;
        CHECK(found);
    }
}

TEST(protocol_values_and_init)
{
    RemoteDevice &d = dev();
    CHECK(d.value_of("Osc/Wave", "Saw") == 1);
    CHECK(d.value_of("Osc/Wave", "3") == 3);
    CHECK(d.value_of("Global/Channel", "02") == 1); // a list counting from "01": text 02 = value 1
    CHECK(d.value_of("Global/Channel", "5") == 4);
    CHECK(d.value_of("Osc/Wave", "Square") == -1 && d.value_of("No/Such", "1") == -1);
    // start-up: the encodable GlobalInit / ParamInit entries in order; the others are left out
    auto init = d.init_messages();
    CHECK(init.size() == 3);
    if (init.size() == 3) {
        CHECK(init[0] == (Bytes{0xF0, 0, 32, 51, 1, 0x10, 115, 0, 10, 1, 0xF7})); // Mode = Multi
        CHECK(init[1][8] == 9 && init[1][9] == 1);                               // Channel = 02
        CHECK(init[2] == (Bytes{0xB2, 18, 1}));                                   // part 3: Osc/Wave = Saw
    }
}

TEST(protocol_automation_list)
{
    const char *xml = R"(<AutomationList>
        <Automation Parameter="Osc/Shape" Part="1" Index="10"/>
        <Automation Parameter="Fx/Mix" Part="2" Index="11"/>
        <Automation Parameter="Fx/Mix" Part="2" Index="12"/>
        <Automation Parameter="" Part="1" Index="13"/>
        <Automation Parameter="Osc/Wave" Part="1"/>
    </AutomationList>)";
    std::vector<AutomationEntry> a;
    std::string err;
    CHECK(load_automation(Bytes(xml, xml + strlen(xml)), a, err));
    CHECK(a.size() == 2);                                // empty path and missing index dropped
    CHECK(a.size() == 2 && a[1].path == "Fx/Mix" && a[1].id == 12); // a later entry for the same (parameter, part) wins
    CHECK(automation_title(dev(), a[0]) == "Osc Shape"); // the short name
    CHECK(automation_title(dev(), a[1]) == "Mix");       // else the name after the last slash
    AutomationEntry plain;
    plain.path = "NoSlash";
    CHECK(automation_title(dev(), plain) == "NoSlash");
    std::vector<AutomationEntry> none;
    CHECK(!load_automation(Bytes{'<', 'x'}, none, err) && !err.empty());
    const char *empty = "<AutomationList/>";
    CHECK(!load_automation(Bytes(empty, empty + strlen(empty)), none, err));
}
