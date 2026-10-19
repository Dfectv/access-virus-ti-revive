#!/usr/bin/env python3
"""checklist.py - the hardware test checklist of a test round as one HTML page (published as a claude.ai artifact).

Before every test round / release the maintainers get one page with a block per system: what to prepare (exact
install commands), the items with steps and the expected result, pass / FAIL / not tested and a note per item, and a
"Copy results" button that gives the text to paste back into the chat. Marks and notes stay in the tester's browser
(the artifact's database under rounds/<round>/marks/<item>, published with capabilities {"db": {}}; localStorage as
the fallback), so they show on every device that opens the link, a republished page with redo items keeps them, and
Claude can read them back with ArtifactData.

  python3 scripts/checklist.py scripts/checklists/round-X.Y.Z.json OUT.html

Spec (JSON): {"round": "0.8.20", "lead": "...", "rules": ["..."],
              "sections": [{"id": "mac", "title": "macOS", "where": "MacBook Air M3, Ableton Live 12",
                            "prep": ["... (HTML allowed)"], "notes": ["..."],
                            "items": [{"id": "M1", "title": "...", "steps": ["..."], "expect": "..."}]}]}
Texts may contain HTML (<code>, <b>); keep them short.
"core": "core.json" in a round spec puts the core items (same every round: cold start, reopen, export, Virus off/on,
sleep) in front of the round's own items in each section with the same id, and the core rules before its rules;
about 60-90 minutes per system in all. "core_items": ["LC1", ...] keeps only these core items; scripts/risk.py
writes it from the round's changed files (risk classes, findings/testing.md).
"""
import html
import json
import os
import sys

CSS = r"""
/* Layout: one column of OS sections, each a short test list with pass / fail / not tested per item */
:root {
  --bg: #f6f5f4; --panel: #ffffff; --fg: #1d1b1c; --muted: #66605f; --line: #ddd8d6;
  --accent: #a3122a; --pass: #1f7a3d; --fail: #b3261e; --skip: #7a6f2a;
  --sans: "Source Sans 3", "Segoe UI", system-ui, sans-serif;
  --mono: "JetBrains Mono", ui-monospace, "Cascadia Mono", monospace;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #161415; --panel: #1f1c1d; --fg: #ece8e6; --muted: #a59d9b; --line: #3a3536;
  --accent: #f0566d; --pass: #5cc781; --fail: #ff7a70; --skip: #d4c35a; color-scheme: dark } }
:root[data-theme="dark"] {
  --bg: #161415; --panel: #1f1c1d; --fg: #ece8e6; --muted: #a59d9b; --line: #3a3536;
  --accent: #f0566d; --pass: #5cc781; --fail: #ff7a70; --skip: #d4c35a; color-scheme: dark }
body { background: var(--bg); color: var(--fg); font-family: var(--sans); font-size: 16px; line-height: 1.5;
  padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px));
  padding-block: 24px 48px; }
main { max-width: 860px; margin: 0 auto; display: grid; gap: 28px; }
h1 { font-size: 1.9rem; margin: 0; text-wrap: balance; }
h2 { font-size: 1.3rem; margin: 0; text-wrap: balance; }
p { margin: 0; }
.lead { color: var(--muted); max-width: 65ch; }
.rules { border-left: 3px solid var(--accent); padding: 4px 0 4px 14px; display: grid; gap: 4px; max-width: 70ch; }
section { display: grid; gap: 14px; }
.sechead { display: flex; flex-wrap: wrap; gap: 8px 16px; align-items: baseline; }
.where { color: var(--muted); font-size: .95rem; }
.prep { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 12px 14px; display: grid; gap: 6px; }
.item { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 14px; display: grid; gap: 8px; min-width: 0; }
.item h3 { font-size: 1.05rem; margin: 0; display: flex; gap: 10px; align-items: baseline; }
.id { font-family: var(--mono); font-size: .85rem; color: var(--accent); font-weight: 600; }
ol { margin: 0; padding-left: 1.3em; display: grid; gap: 3px; }
.expect { font-size: .95rem; }
.expect b { color: var(--muted); font-weight: 600; }
code { font-family: var(--mono); font-size: .85em; background: color-mix(in srgb, var(--line) 55%, transparent); padding: 1px 4px; border-radius: 3px; overflow-wrap: anywhere; }
.verdict { display: flex; flex-wrap: wrap; gap: 6px; }
.verdict label { position: relative; border: 1px solid var(--line); border-radius: 999px; padding: 2px 12px; cursor: pointer; font-size: .9rem; user-select: none; }
.verdict input { position: absolute; opacity: 0; pointer-events: none; }
.verdict input:focus-visible + span { outline: 2px solid var(--accent); outline-offset: 3px; }
.verdict label:has(input[value="pass"]:checked) { border-color: var(--pass); color: var(--pass); font-weight: 600; }
.verdict label:has(input[value="fail"]:checked) { border-color: var(--fail); color: var(--fail); font-weight: 600; }
.verdict label:has(input[value="skip"]:checked) { border-color: var(--skip); color: var(--skip); font-weight: 600; }
textarea { width: 100%; box-sizing: border-box; min-height: 2.4em; font: inherit; font-size: .95rem; color: var(--fg);
  background: var(--bg); border: 1px solid var(--line); border-radius: 4px; padding: 6px 8px; resize: vertical; }
textarea:focus-visible, button:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
.bar { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }
button { font: inherit; font-weight: 600; background: var(--accent); color: var(--bg); border: 0; border-radius: 4px; padding: 8px 16px; cursor: pointer; }
#out { min-height: 8em; font-family: var(--mono); font-size: .8rem; }
.count { font-variant-numeric: tabular-nums; color: var(--muted); font-size: .95rem; }
.note { color: var(--muted); font-size: .9rem; }
"""

JS = r"""
(function () {
  var KEY = __KEY__, ROUND = __ROUND__;
  var state = {};
  try { state = JSON.parse(localStorage.getItem(KEY) || '{}') || {}; } catch (e) { state = {}; }
  function save() { try { localStorage.setItem(KEY, JSON.stringify(state)); } catch (e) {} }
  var items = Array.prototype.slice.call(document.querySelectorAll('.item'));
  var names = { pass: 'pass', fail: 'FAIL', skip: 'not tested' };
  // The artifact's database keeps the marks across browsers and devices (the real PC runs Windows without
  // Claude); localStorage stays as the fallback when the page runs without it. Writes go one after another (a
  // burst of parallel writes, e.g. carrying a browser's marks over, is refused by the store's rate limit).
  var marks = null, chain = Promise.resolve(), timer = {};
  function put(id) {
    if (!marks) return;
    chain = chain.then(function () {
      var s = state[id] || {};
      return marks.doc(id).set({ v: s.v || '', n: s.n || '' });
    }).catch(function () { status('Could not save to the page; kept in this browser.'); });
  }
  function changed(id, later) {
    save(); render();
    clearTimeout(timer[id]);
    if (later) timer[id] = setTimeout(function () { timer[id] = 0; put(id); }, 800); else put(id);
  }
  function status(t) { document.getElementById('saved').textContent = t; }
  items.forEach(function (it) {
    var id = it.dataset.id, s = state[id] || {};
    var v = document.createElement('div'); v.className = 'verdict'; v.setAttribute('role', 'radiogroup');
    [['pass', 'pass'], ['fail', 'fail'], ['skip', 'not tested']].forEach(function (o) {
      var l = document.createElement('label'), i = document.createElement('input'), sp = document.createElement('span');
      i.type = 'radio'; i.name = 'v-' + id; i.id = 'v-' + id + '-' + o[0]; i.value = o[0]; i.checked = s.v === o[0];
      sp.textContent = o[1]; l.appendChild(i); l.appendChild(sp); v.appendChild(l);
      i.addEventListener('change', function () { (state[id] = state[id] || {}).v = o[0]; changed(id, false); });
    });
    var t = document.createElement('textarea'); t.id = 'n-' + id; t.placeholder = 'Note (optional)'; t.value = s.n || '';
    t.setAttribute('aria-label', 'Note for ' + id);
    t.addEventListener('input', function () { (state[id] = state[id] || {}).n = t.value; changed(id, true); });
    it.appendChild(v); it.appendChild(t);
  });
  function show(id) {
    var s = state[id] || {}, t = document.getElementById('n-' + id);
    ['pass', 'fail', 'skip'].forEach(function (k) {
      var r = document.getElementById('v-' + id + '-' + k); if (r) r.checked = s.v === k;
    });
    if (t && document.activeElement !== t) t.value = s.n || '';
  }
  function render() {
    var lines = [__RESULTS__], done = 0;
    document.querySelectorAll('section[id]').forEach(function (sec) {
      var its = sec.querySelectorAll('.item'); if (!its.length) return;
      lines.push('', sec.querySelector('h2').textContent + ':');
      its.forEach(function (it) {
        var id = it.dataset.id, s = state[id] || {};
        if (s.v) done++;
        lines.push(id + ' ' + it.querySelector('h3 span:last-child').textContent + ': ' + (s.v ? names[s.v] : '-') +
                   (s.n && s.n.trim() ? ' - ' + s.n.trim() : ''));
      });
    });
    document.getElementById('out').value = lines.join('\n');
    document.getElementById('count').textContent = done + ' of ' + items.length + ' marked';
  }
  render();
  status('Saved in this browser only.');
  var use = window.claude && window.claude.use ? window.claude.use('db') : Promise.resolve(null);
  Promise.resolve(use).then(function (db) {
    if (!db) return;
    marks = db.collection('rounds/' + ROUND + '/marks');
    var first = true;
    marks.onSnapshot(function (snap) {
      if (snap.metadata.fromCache) return;
      if (first && snap.empty) {
        // a page that was used before the database: carry this browser's marks over once
        Object.keys(state).forEach(function (id) { if (state[id].v || state[id].n) put(id); });
      }
      first = false;
      snap.docs.forEach(function (d) {
        var b = d.data() || {}, id = d.id;
        if (timer[id] && document.activeElement === document.getElementById('n-' + id)) return;  // typing here
        state[id] = { v: b.v || undefined, n: b.n || '' }; show(id);
      });
      save(); render();
      status('Saved with the page: the marks show on every device that opens this link.');
    }, function () { status('Saved in this browser only (the page store is not available).'); });
  }, function () {});
  document.getElementById('copy').addEventListener('click', function () {
    var out = document.getElementById('out'), msg = document.getElementById('copied');
    var p = navigator.clipboard && navigator.clipboard.writeText ? navigator.clipboard.writeText(out.value) : Promise.reject();
    p.then(function () { msg.textContent = 'Copied.'; }, function () { out.select(); msg.textContent = 'Selected, press Ctrl+C.'; });
  });
})();
"""


def item(it):
    steps = "".join("<li>%s</li>" % s for s in it["steps"])
    return ("    <div class=\"item\" data-id=\"%s\">\n"
            "      <h3><span class=\"id\">%s</span><span>%s</span></h3>\n"
            "      <ol>%s</ol>\n"
            "      <p class=\"expect\"><b>Expected:</b> %s</p>\n"
            "    </div>\n") % (html.escape(it["id"]), html.escape(it["id"]), it["title"], steps, it["expect"])


def section(sec):
    out = ["  <section id=\"%s\">\n" % html.escape(sec["id"]),
           "    <div class=\"sechead\"><h2>%s</h2><span class=\"where\">%s</span></div>\n" % (sec["title"], sec.get("where", ""))]
    if sec.get("prep") or sec.get("notes"):
        out.append("    <div class=\"prep\">\n")
        out += ["      <p>%s</p>\n" % p for p in sec.get("prep", [])]
        out += ["      <p class=\"note\">%s</p>\n" % p for p in sec.get("notes", [])]
        out.append("    </div>\n")
    out += [item(it) for it in sec["items"]]
    out.append("  </section>\n\n")
    return "".join(out)


def page(spec):
    rnd = spec["round"]
    ids = [it["id"] for sec in spec["sections"] for it in sec["items"]]
    if len(ids) != len(set(ids)):
        sys.exit("checklist.py: item ids must be unique")
    rules = "".join("      <p>%s</p>\n" % r for r in spec.get("rules", []))
    return ("<title>Round %s Checklist</title>\n" % html.escape(rnd)
            + "<meta name=\"claude:viewport-fit-sides\" content=\"cover\">\n"
            + "<link rel=\"stylesheet\" href=\"https://fonts.googleapis.com/css2?family=Source+Sans+3:wght@400;600;700"
              "&family=JetBrains+Mono:wght@400;600&display=swap\">\n"
            + "<style>" + CSS + "</style>\n\n<main>\n"
            + "  <header style=\"display:grid;gap:10px\">\n    <h1>Round %s checklist</h1>\n" % html.escape(rnd)
            + "    <p class=\"lead\">%s</p>\n" % spec.get("lead", "")
            + "    <div class=\"rules\">\n" + rules + "    </div>\n  </header>\n\n"
            + "".join(section(s) for s in spec["sections"])
            + "  <section id=\"results\">\n"
              "    <div class=\"sechead\"><h2>Results</h2><span class=\"count\" id=\"count\"></span></div>\n"
              "    <div class=\"bar\"><button type=\"button\" id=\"copy\">Copy results</button>"
              "<span class=\"note\" id=\"copied\" role=\"status\"></span></div>\n"
              "    <textarea id=\"out\" readonly aria-label=\"Results as text\"></textarea>\n"
              "    <p class=\"note\" id=\"saved\" role=\"status\"></p>\n  </section>\n</main>\n\n"
            + "<script>" + JS.replace("__KEY__", json.dumps("tirevive-round-" + rnd))
                             .replace("__ROUND__", json.dumps(rnd))
                             .replace("__RESULTS__", json.dumps("Round " + rnd + " results")) + "</script>\n")


def with_core(spec, spec_path):
    """the round spec with the core checklist (spec["core"], relative to the spec file) merged in"""
    if not spec.get("core"):
        return spec
    with open(os.path.join(os.path.dirname(spec_path), spec["core"]), encoding="utf-8") as f:
        core = json.load(f)
    keep = spec.get("core_items")  # scripts/risk.py: only the core items the round's changes need
    items = {s["id"]: [it for it in s["items"] if keep is None or it["id"] in keep] for s in core["sections"]}
    spec["rules"] = core.get("rules", []) + spec.get("rules", [])
    for s in spec["sections"]:
        s["items"] = items.get(s["id"], []) + s.get("items", [])
    return spec


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], encoding="utf-8") as f:
        spec = with_core(json.load(f), sys.argv[1])
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write(page(spec))
