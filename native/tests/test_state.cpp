// project state container, editor-only records, meta parameter mapping
#include "check.h"
#include "model/state.h"
#include <cstring>

using namespace vti;
using Bytes = std::vector<uint8_t>;

TEST(container_round_trip)
{
    std::vector<Bytes> recs = {{}, {0xB0, 7, 100}, Bytes(524, 0x55), {0xF0, 0x7D, 0xF7}};
    auto d = build_container(recs);
    CHECK(d.size() == 8 + 4 * recs.size() + 3 + 524 + 3);
    CHECK(d[0] == 'M' && d[3] == 'I');
    CHECK(parse_container(d) == recs);
}

TEST(container_damaged)
{
    auto d = build_container({{1, 2, 3}, {4, 5, 6, 7}});
    CHECK(parse_container(Bytes(d.begin(), d.end() - 2)).size() == 1); // a truncated last record is dropped
    Bytes bad = d;
    bad[0] = 'X';
    CHECK(parse_container(bad).empty());
    CHECK(parse_container({}).empty());
    Bytes huge = d;
    huge[8] = 0x7F; // first record claims ~2 GB
    CHECK(parse_container(huge).empty());
}

TEST(part_dump_detection)
{
    Bytes m(524, 0);
    m[0] = 0xF0, m[6] = 0x10, m[7] = 0x00, m[8] = 5;
    CHECK(is_part_dump(m, 5));
    CHECK(!is_part_dump(m, 4));
    m.resize(267);
    CHECK(!is_part_dump(m, 5));
}

TEST(editor_record_round_trip)
{
    auto r = editor_record("VirusControl Links/LFO 1 Link", 3, 1);
    CHECK(r.front() == 0xF0 && r.back() == 0xF7 && r[1] == 0x7D);
    std::string path;
    int part = -1, value = -1;
    CHECK(parse_editor_record(r, path, part, value));
    CHECK(path == "VirusControl Links/LFO 1 Link" && part == 3 && value == 1);
    r = editor_record("A/B", 15, 200); // 7-bit on the wire
    CHECK(parse_editor_record(r, path, part, value) && value == (200 & 0x7F));
    CHECK(!parse_editor_record({0xF0, 0x00, 0x20, 0x33, 1, 2, 3, 4, 5, 6, 0xF7}, path, part, value)); // Access SysEx
    CHECK(!parse_editor_record({0xF0, 0x7D, 'V', 'T', 'N', 1, 0, 'A', 'B'}, path, part, value));       // cut off
}

TEST(meta_mapping)
{
    MetaClient lin; // client = 2 * meta + 1
    lin.a = 2, lin.b = 1;
    CHECK_EQ(meta_to_client(lin, 10), 21);
    int mv = -1;
    CHECK(client_to_meta(lin, 21, mv) && mv == 10);
    MetaClient mapped;
    mapped.maps = {{0, 5}, {64, 6}, {127, 7}};
    CHECK_EQ(meta_to_client(mapped, 6), 64);
    CHECK(client_to_meta(mapped, 127, mv) && mv == 7);
    CHECK(!client_to_meta(mapped, 3, mv)); // a client value without map entry does not move the meta
    MetaClient flat;
    flat.a = 0;
    CHECK(!client_to_meta(flat, 5, mv)); // not invertible
}

TEST(state_part_dumps_upgraded)
{
    // E174: the Virus converts a version-9 part dump itself and resets the newer fields (Input Mode, unison,
    // envelopes 3/4 ...) - so the state keeps part dumps in the current format; the editor's values then reach it
    std::vector<uint8_t> d(524, 0);
    const uint8_t head[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x00, 0x03, 9};
    memcpy(d.data(), head, sizeof head);
    d[523] = 0xF7;
    std::vector<uint8_t> stored = d;
    stored[7] = 0x01; // a stored bank patch (not an edit buffer): the firmware converts those itself
    std::vector<uint8_t> current = d;
    current[9] = 12;
    std::vector<std::vector<uint8_t>> recs = {d, stored, current, {0xB0, 7, 100}};
    CHECK(upgrade_state_dumps(recs) == 1);
    CHECK(recs[0][9] == 12);                       // the edit-buffer dump of part 4: upgraded
    CHECK(recs[1] == stored);                       // stored patch untouched
    CHECK(recs[2] == current);                      // already current
    CHECK(recs[3] == (std::vector<uint8_t>{0xB0, 7, 100}));
    CHECK(upgrade_state_dumps(recs) == 0);         // idempotent
}
