#!/usr/bin/env python3
# build.py - the TI Revive website (GitHub Pages) from the GitHub releases and the texts below: one page
# (index.html) and the changelog archive. Static, no JavaScript, no external fonts or trackers. site/src/ holds the
# page frame (base.html), the style sheet and the plug-in screenshot; the Control Center screenshots are the
# README's (docs/images). Deployed by .github/workflows/site.yml.
#
#   python3 scripts/site/build.py [--releases FILE] [--checksums] [--out DIR]
#
# --releases: a saved `gh api repos/derpsn/access-virus-ti-revive/releases?per_page=100` (else gh is called)
# --checksums: download the newest release's files once (cached in --cache) and show their SHA-256
import argparse
import hashlib
import html
import json
import os
import re
import shutil
import subprocess
import sys

REPO = "derpsn/access-virus-ti-revive"
GH = "https://github.com/" + REPO
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# versions whose release text has no "New in" / "What's new in" part: a short summary written by hand
CHANGELOG_EXTRA = {
    "v0.8.1": "Native **Audio Unit** for Logic Pro and GarageBand (the projects of the classic unit open with it); "
              "the macOS package holds only the plug-in (VST3 + AU) and the USB service, the Access `.pkg` is only "
              "unpacked.",
    "v0.8.0": "**TI Revive Virus Control**: a plug-in written from scratch that looks and works like the original "
              "Virus Control and runs natively on Windows, macOS (Apple Silicon and Intel) and Linux, without Rosetta, "
              "Wine or yabridge. Projects of the classic plug-in open with it.",
    "v0.2.0": "First public release: Windows 10/11 x64 installer for the original Access plug-in without the old "
              "kernel driver.",
}


# --------------------------------------------------------------------------------------------- small markdown
def inline(t):
    t = html.escape(t, quote=False)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    t = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", t)
    t = re.sub(r"(?<![*\w])\*([^*\s][^*]*)\*(?!\*)", r"<em>\1</em>", t)
    t = re.sub(r"\[([^\]]+)\]\((https?://[^)\s]+)\)", r'<a href="\2">\1</a>', t)
    return t


def md(text):
    """paragraphs and '- ' lists; enough for the release texts"""
    out, para, items = [], [], []

    def flush():
        if para:
            out.append("<p>" + inline(" ".join(para)) + "</p>")
            para.clear()
        if items:
            out.append("<ul>" + "".join("<li>" + inline(i) + "</li>" for i in items) + "</ul>")
            items.clear()

    for line in text.replace("\r", "").split("\n"):
        s = line.strip()
        if not s:
            flush()
        elif s.startswith("- "):
            if para:
                flush()
            items.append(s[2:])
        elif items and line.startswith("  "):
            items[-1] += " " + s
        else:
            if items:
                flush()
            para.append(s)
    flush()
    return "\n".join(out)


# ------------------------------------------------------------------------------------------------ releases
def load_releases(path):
    if path:
        with open(path) as f:
            return json.load(f)
    out = subprocess.run(["gh", "api", "repos/%s/releases?per_page=100" % REPO], check=True, capture_output=True,
                         text=True).stdout
    return json.loads(out)


def summary(rel):
    """the part of a release text that is new in that version"""
    tag, body = rel["tag_name"], rel.get("body") or ""
    if tag in CHANGELOG_EXTRA:
        return CHANGELOG_EXTRA[tag]
    body = body.replace("\r", "")
    m = re.search(r"^\*\*New in [^*]*\*\*.*?(?=^\*\*Since |^\*\*Windows |^\*\*macOS |^\*\*Linux |^\*\*Coming |\Z)",
                  body, re.M | re.S)
    if m:
        return re.sub(r"^\*\*New in [^*]*\*\*\s*", "", m.group(0).strip(), count=1)
    m = re.search(r"^### What's new in[^\n]*\n(.*?)(?=^\*\*TI Revive \d|^### |\Z)", body, re.M | re.S)
    if m:
        return m.group(1).strip()
    first = [p for p in body.split("\n\n") if p.strip() and not p.startswith("**TI Revive")]
    return first[0] if first else ""


def title_of(rel):
    """the short headline of a version ('steadier connection'), if its text has one"""
    body = (rel.get("body") or "").replace("\r", "")
    m = re.search(r"^\*\*New in [\d.]+: ([^*]+?)\.?\*\*", body, re.M) or \
        re.search(r"^### What's new in [\d.]+[: ]+([^\n]+)", body, re.M)
    return m.group(1).strip(" .:") if m else ""


def sha256_of_assets(rel, cache):
    sums = {}
    d = os.path.join(cache, rel["tag_name"])
    os.makedirs(d, exist_ok=True)
    for a in rel["assets"]:
        p = os.path.join(d, a["name"])
        if not os.path.exists(p) or os.path.getsize(p) != a["size"]:
            subprocess.run(["gh", "release", "download", rel["tag_name"], "-R", REPO, "-p", a["name"], "-D", d,
                            "--clobber"], check=True)
        h = hashlib.sha256()
        with open(p, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        sums[a["name"]] = h.hexdigest()
    return sums


def size(n):
    return "%.1f MB" % (n / 1e6) if n >= 1e6 else "%d KB" % max(1, n // 1000)


def date(iso):
    y, m, d = iso[:10].split("-")
    months = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec".split()
    return "%d %s %s" % (int(d), months[int(m) - 1], y)


def asset(rel, suffix):
    for a in rel["assets"]:
        if a["name"].endswith(suffix):
            return a
    return None


# ------------------------------------------------------------------------------------------------ the pages
def highlights(rel, n=3):
    """the first points of a version's summary (its list items, else its first sentences)"""
    text = summary(rel).replace("\r", "")
    items = [l.strip()[2:] for l in text.split("\n") if l.strip().startswith("- ")]
    if not items:
        first = re.split(r"(?<=[.!?])\s+", " ".join(text.split()))
        items = [" ".join(first[:2])] if first and first[0] else []
    out = []
    for i in items[:n]:
        i = re.sub(r"\s*\(E\d+[^)]*\)", "", i) # evidence numbers mean nothing to a visitor
        out.append(i if len(i) <= 220 else i[:217].rsplit(" ", 1)[0] + " ...")
    return out


def os_card(name, req, a, alt, sums, steps):
    if not a:
        return ""
    s = sums.get(a["name"])
    alt_html = (f'<p class="alt">or <a href="{alt["browser_download_url"]}">{alt["name"]}</a> '
                f'({size(alt["size"])})</p>') if alt else ""
    return f"""
    <div class="os">
      <h3>{name}</h3>
      <p class="req">{req}</p>
      <a class="button" href="{a["browser_download_url"]}">Download <small>{size(a["size"])}</small></a>
      <p class="alt"><code>{a["name"]}</code></p>{alt_html}
      <details><summary>How to install</summary><ol>{"".join("<li>" + x + "</li>" for x in steps)}</ol>
      {f'<code class="sum" title="SHA-256">SHA-256 {s}</code>' if s else ""}</details>
    </div>"""


def page_landing(latest, releases, sums):
    v = latest["tag_name"][1:]
    when = date(latest["published_at"])
    news = []
    for r in [r for r in releases if not r.get("prerelease") and not r.get("draft")][:3]:
        t = title_of(r) or "Release " + r["tag_name"][1:]
        pts = "".join("<li>" + inline(i) + "</li>" for i in highlights(r))
        news.append(f"""
    <article class="rel">
      <div><div class="v"><a href="changelog.html#{html.escape(r["tag_name"])}">{html.escape(r["tag_name"][1:])}</a></div>
      <time datetime="{r["published_at"][:10]}">{date(r["published_at"])}</time></div>
      <div><h3>{html.escape(t[:1].upper() + t[1:])}</h3><ul>{pts}</ul></div>
    </article>""")
    win = os_card("Windows", "Windows 11 x64, also Windows 10", asset(latest, "-setup.exe"), asset(latest, ".msi"),
                  sums, ["Put the setup file next to your Access &ldquo;Virus TI Software Suite&rdquo; 64-bit "
                         "<code>.msi</code>.",
                         "Run it. It is not code-signed: <em>More info <span class='sep'>&rsaquo;</span> Run anyway</em>. Your Access installer "
                         "is only unpacked, not installed.",
                         "Connect the Virus, rescan plug-ins in your DAW and load <strong>TI Revive Virus "
                         "Control</strong>. The ASIO device <strong>TI Revive Virus Audio</strong> makes the Virus a "
                         "sound card."])
    mac = os_card("macOS", "macOS 14.4 or newer, Apple Silicon and Intel", asset(latest, "-mac.zip"), None, sums,
                  ["Unpack the zip and open <code>TI Revive.pkg</code> (not signed: right-click <span class='sep'>&rsaquo;</span> "
                   "<em>Open</em>).",
                   "Put your Access <code>.pkg</code> next to <code>Set up Access Virus TI files.command</code> and "
                   "run it. It also parks old drivers that would block the Virus.",
                   "Start your DAW and allow microphone access when asked: macOS gives the audio of the Virus only "
                   "to apps with it. VST3 and Audio Unit."])
    lin = os_card("Linux", "x86_64 with systemd, Ubuntu 22.04 / Debian 12 or newer", asset(latest, "-linux.tar.gz"),
                  None, sums,
                  ["<code>sudo apt install msitools</code> to unpack the Access installer.",
                   "Unpack and run <code>./install.sh \"/path/to/Virus TI Software Suite 64-bit.msi\"</code>; it "
                   "asks for your password once (udev rule).",
                   "For steady audio join the real-time group (<code>sudo usermod -aG pipewire $USER</code>, log "
                   "out and in), then rescan plug-ins."])
    return f"""
<section class="hero">
  <div class="wrap">
    <div class="hero-text">
    <h1>TI Revive</h1>
    <p class="tagline">Like meeting an old best friend again.</p>
    <p class="lead"><strong>Total Integration for the Access Virus TI on Windows 11, macOS and Linux, without
    the old drivers.</strong> Its plug-in looks and works like the original Virus Control: every parameter, the preset
    browser, automation, and the synth's audio over the same USB cable.</p>
    <p class="actions"><a class="button" href="#download">Download {v}</a>
    <a class="second" href="#news">What's new</a></p>
    <p class="facts-line">Free &middot; open source &middot; VST3 and Audio Unit &middot; released {when}</p>
    <p class="thanks">&#9749; <strong>Free, and it will stay free.</strong> Every change is tested on the real synth
    on Windows, macOS and Linux, often back and forth between the three, and that takes a lot of evenings. If TI Revive
    gives your Virus a second life and you'd like to say thanks, you can <a href="https://buymeacoffee.com/callmederpsn">buy me a coffee</a>.
    It is entirely optional; every feature works the same either way. A test report or a kind word helps just as much.</p>
    </div>
    <figure class="shot">
      <img src="plugin-filter.png" width="1280" height="724"
      alt="TI Revive Virus Control, the plug-in's filter page, drawn from the user's own Access files">
      <figcaption>TI Revive is not affiliated with Access Music or Kemper and contains no Access files.</figcaption>
    </figure>
  </div>
</section>

<section class="block" id="original">
  <div class="wrap">
    <h2>Problems in the original Access plug-in</h2>
    <p class="intro">Did the original Access plug-in, <strong>Virus Control</strong>, the one that came with the Virus
    TI, have problems for you? TI Revive rebuilds that plug-in, and we want to fix its problems instead of copying
    them.</p>
    <div class="original">
      <div>
        <h3>Already fixed</h3>
        <ul>
          <li>In our timing test the original plug-in played the first note late and dropped the second. TI Revive puts all 256
          notes on the grid.</li>
          <li>While it starts up or syncs banks, the original plug-in drops the notes you play and sends &ldquo;All Notes
          Off&rdquo;. TI Revive holds the notes back at start-up and pauses its bank sync while you play.</li>
        </ul>
      </div>
      <div>
        <h3>What to report</h3>
        <p>Hanging notes, sync errors, crashes, projects that did not open, the preset browser, automation, timing.
        If you only remember it roughly, say so.</p>
        <p class="actions"><a class="button" href="{GH}/issues/new?template=original_problem.yml">Report a problem in
        the original plug-in</a> <a class="second" href="{GH}/issues?q=label%3Aoriginal">Reports so far</a></p>
      </div>
    </div>
  </div>
</section>

<section class="block" id="download">
  <div class="wrap">
    <h2>Version {v}, released {when}</h2>
    <p class="intro">You also need your own Access &ldquo;Virus TI Software Suite&rdquo; installer (e.g. 5.1.7).
    Access offers it on its <a href="https://www.virus.info/downloads">download page</a>; you need an account there to
    download it.
    TI Revive unpacks only what the plug-in reads from it: the skin, the parameter descriptions, the ROM banks.
    Older versions are on <a href="{GH}/releases">GitHub</a>.</p>
    <div class="oses">{win}{mac}{lin}
    </div>
    <div class="needs">
      <div><b>Synth</b><span>Virus TI and TI2: Desktop, Polar, Keyboard, and the TI Snow</span></div>
      <div><b>DAW</b><span>Any VST3 host; Audio Unit for Logic and GarageBand</span></div>
      <div><b>Outputs</b><span>USB 1 and 2, USB 3 if you switch it on in PATCH UTILITY <span class='sep'>&rsaquo;</span> Preferences</span></div>
      <div><b>Not included</b><span>Firmware or OS updates for the Virus, or anything else that writes its flash.
      On purpose.</span></div>
    </div>
  </div>
</section>

<section class="block" id="how">
  <div class="wrap">
    <h2>How it works</h2>
    <p class="intro">The plug-in talks to the Virus directly over USB, so there is no MIDI port to set up and no Access driver left on your system.</p>
    <div class="steps">
      <div><h3>Install TI Revive</h3><p>One installer per system. It sets up the USB access and puts the plug-in where
      your DAW looks for it.</p></div>
      <div><h3>Point it at your Access installer</h3><p>The setup unpacks the skin, the parameter descriptions and the
      ROM banks from the installer you own. No Access files come with TI Revive.</p></div>
      <div><h3>Load the plug-in</h3><p><strong>TI Revive Virus Control</strong> on a MIDI track. Projects made with the
      original plug-in open with it. The first start reads RAM A&ndash;D from the Virus once, which takes a few
      minutes; you can play meanwhile.</p></div>
    </div>
  </div>
</section>

{features_section()}

<section class="block" id="status">
  <div class="wrap">
    <h2>What is tested where</h2>
    <p class="intro">We test on a Virus TI2 Desktop. For other models, systems and DAWs we depend on
    <a href="{GH}/issues/new?template=test_report.yml">test reports</a> from their owners.</p>
    <table class="matrix">
      <thead><tr><th>Model</th><th>Windows</th><th>macOS</th><th>Linux</th></tr></thead>
      <tbody>
        <tr><th>TI2 Desktop</th><td class="st ok">tested</td><td class="st ok">tested</td><td class="st ok">tested</td></tr>
        <tr><th>TI Snow</th><td class="st no">no report yet</td><td class="st rep">works for users</td>
          <td class="st no">no report yet</td></tr>
        <tr><th>Desktop (TI1), Polar, Keyboard</th><td class="st no">no report yet</td><td class="st no">no report yet</td>
          <td class="st no">no report yet</td></tr>
      </tbody>
    </table>
    <ul class="limits">
      <li>The USB audio <em>input</em> of the Virus (&ldquo;3 Outs / 1 Input&rdquo;) is not fed by the plug-in yet.</li>
      <li>The installers are not signed, so Windows and macOS ask once before they run them.</li>
    </ul>
    <div class="testing">
      <div><h3>Automated tests</h3><p>{TESTS} unit tests run with every release build on Windows and Linux. They
      cover {COVERAGE} of the lines of the core library: the USB protocol, the device access, the editor and the reader
      for the Access files (measured {COVERAGE_DATE}).</p></div>
      <div><h3>On the synth</h3><p>Changes that touch the Virus are tried on our TI2 Desktop before a release; the
      release notes say on which systems and in which DAWs.</p></div>
      <div><h3>Your reports</h3><p>The soft knobs (0.8.7, 0.8.8) and the third USB output (0.8.9) were fixed after a
      user's report. <a href="#support">Bug reports</a> and <a href="{GH}/issues/new?template=test_report.yml">test
      reports</a> from other setups show us what to fix next.</p></div>
    </div>
  </div>
</section>

<section class="block" id="news">
  <div class="wrap">
    <h2>Recent releases</h2>
    <div class="news">{"".join(news)}
    </div>
    <p class="more"><a href="changelog.html">Every version</a>
    <a href="{GH}/releases.atom">Release feed (Atom)</a>
    <a href="{GH}">On GitHub: <em>Watch <span class='sep'>&rsaquo;</span> Custom <span class='sep'>&rsaquo;</span> Releases</em></a></p>
  </div>
</section>

<section class="block" id="help">
  <div class="wrap">
    <h2>Help and support</h2>
    <div class="faq">
      <details><summary>&ldquo;Virus TI not connected&rdquo;</summary>
      <p>Only one driver can use the USB interfaces of the Virus. Old Access drivers, or third-party ones like the driver of
      another Virus editor, take them first. Disable the other driver and restart; on macOS the setup script offers to
      park the known ones.</p></details>
      <details><summary>Connected, but no sound</summary>
      <p>The Virus decides per part where its sound goes. Switch the part's output to &ldquo;Out 1&rdquo; and back to
      &ldquo;USB 1&rdquo; in the plug-in to send the setting again. On macOS also check that your DAW may use the
      microphone.</p></details>
      <details><summary>Three separate outputs (USB 3)</summary>
      <p>PATCH UTILITY <span class='sep'>&rsaquo;</span> Preferences <span class='sep'>&rsaquo;</span> &ldquo;Input and Output Configuration&rdquo; = &ldquo;3 Outs / No
      Input&rdquo;. Then a part's output can be &ldquo;USB3 L+R&rdquo; and its sound comes out of the plug-in's Out 3.
      In Ableton: an audio track with <em>Audio From</em> = the Virus track, <em>Out 3</em>, <em>Monitor In</em>.</p>
      </details>
      <details><summary>Process audio through the Virus (sidechain input)</summary>
      <p>Like the original, the plug-in can send audio from your DAW into the Virus: set &ldquo;Input and Output Configuration&rdquo; to &ldquo;3 Outs / 1 Input&rdquo;, and on the FX 1 page set a part's Input Mode to Static (always on) or Dynamic (played with notes) and Select to L + R. Then send an audio track to the plug-in's sidechain input; the part's filters and effects process it. In REAPER, give the Virus track 4 channels and send the audio to channels 3/4 (check in the pin connector that they feed Sidechain 1/2). In Ableton Live, pick the audio track in the device's Sidechain section. In this mode the Virus does not work as a sound card for your computer. Tested on Linux (REAPER), macOS (Ableton Live 12) and in a Windows 11 VM (Ableton Live 11); not yet on a real Windows PC.</p></details>
      <details><summary>Projects bring back every patch setting</summary>
      <p>Older versions saved the parts in a format the Virus converts itself, and it reset about 20 newer settings on
      loading: Input Mode, unison, arpeggiator mode, chorus type, envelopes 3 and 4 and more. Projects now keep the
      current format (0.8.18); older projects are converted when they open, and settings that never reached the Virus
      open as the Virus played them. A new project's chorus shows &ldquo;Classic&rdquo;: that is what the Virus makes of
      the original's Init patch.</p></details>
      <details><summary>When the Virus is missing</summary>
      <p>Switched off or unplugged, the editor shows the original's &ldquo;No Virus TI can be found&rdquo; page and the
      page tabs are locked, as in the original. The page goes away by itself when the Virus is back. Do not follow its
      advice to hold ARP EDIT while switching the Virus on; TI Revive does not need that.</p></details>
      <details><summary>The first notes after Play are late</summary>
      <p>The plug-in sends notes slightly ahead to make up for the USB round trip, so start playback a little before
      the first note. The original driver did the same.</p></details>
    </div>
    <div class="support" id="support">
      <h3>Report a problem</h3>
      <p>If your question is not answered above, open a report on GitHub. Reports are read by the developer, and the
      answer comes in the same thread; it is a hobby project, so it can take a day or two.</p>
      <ol>
        <li><strong>Collect the logs.</strong> Easiest: open the <strong>TI Revive Control Center</strong>, switch on
        <em>Nerd mode</em> and press the <em>Save logs...</em> button. You get the plug-in and broker logs in one file,
        without your home folder or user name. Or by hand: create an empty file <code>diag.on</code> in the plug-in's folder,
        reproduce the problem, and take <code>diag.log</code> from the same folder:
        <div class="paths"><b>Windows</b><code>%LOCALAPPDATA%\\virus-ti-native</code>
        <b>macOS</b><code>~/Library/Application Support/virus-ti-native</code>
        <b>Linux</b><code>~/.local/share/virus-ti-native</code></div></li>
        <li><strong>Open a <a href="{GH}/issues/new?template=bug_report.yml">bug report</a></strong>. The form asks
        for your system, Virus model, the TI Revive and DAW versions, and what happened. Drag the log files into
        it.</li>
        <li><strong>Everything works?</strong> A <a href="{GH}/issues/new?template=test_report.yml">test report</a>
        helps as much as a bug report, above all for the Snow, Polar and Keyboard, and DAWs we have not
        tried.</li>
      </ol>
      <p class="small">You need a free GitHub account to post. Look through the logs before you attach them and remove
      anything you consider private. Existing reports: <a href="{GH}/issues">all issues</a>.</p>
    </div>
  </div>
</section>

<section class="block" id="contribute">
  <div class="wrap">
    <h2>Contributing</h2>
    <p class="intro">The repository is prepared for working on it with an AI coding assistant.</p>
    <div class="testing">
      <div><h3>For AI assistants</h3><p><a href="{GH}/blob/main/AGENTS.md">AGENTS.md</a> gives any assistant the
      rules and a map of the repository: no Access material in the code, never risk the synth, evidence before claims,
      and say exactly what was tested.</p></div>
      <div><h3>Skills</h3><p>Step-by-step guides in <a href="{GH}/tree/main/.claude/skills">.claude/skills</a>:
      test your setup safely and write a report, find the cause of a problem in the logs, and prepare a pull request
      the way we review it. They are plain text, so they work without an assistant too.</p></div>
      <div><h3>Without code</h3><p>Test reports, reports about problems in the original, and help with reading
      someone's logs are as useful as pull requests.</p></div>
    </div>
    <p class="actions contribute-actions"><a class="button" href="{GH}/blob/main/CONTRIBUTING.md">How to
    contribute</a> <a class="second" href="{GH}">Open the repository</a></p>
  </div>
</section>

<section class="block" id="about">
  <div class="wrap">
    <div class="about">
      <div><h3>Why</h3><p>The Access drivers stopped working on current systems. TI Revive is free and open
      source.</p></div>
      <div><h3>How it was built</h3><p>By working out the USB protocol and the original software's behaviour from
      logs, captures and analysis, and by testing on real hardware. Much of the work was done with an AI coding
      assistant; the tests on the synth are done by hand.</p></div>
    </div>
  </div>
</section>
"""


def features_section():
    return f"""
<section class="block" id="features">
  <div class="wrap">
    <h2>Features</h2>
    <p class="intro">What TI Revive does, and what we measured on our own Virus TI2 Desktop. The raw numbers and how
    they were taken are in the repository (<a href="{GH}/blob/main/findings/evidence.md">evidence</a>,
    <a href="{GH}/blob/main/native/docs/status.md">development log</a>).</p>

    <div class="feature">
      <div><h3>The plug-in</h3>
      <p><strong>TI Revive Virus Control</strong> is written from scratch and looks and works like the original Access plug-in, Virus Control. It draws its look from your own Access installer at runtime.</p></div>
      <ul>
        <li>All 16 parts, every parameter, the preset browser, RAM and ROM banks, patch libraries.</li>
        <li>Projects made with the original plug-in open with it; the saved state is byte for byte the same.</li>
        <li>Presets and soft knobs chosen on the Virus itself are followed, as in the original.</li>
        <li>Three stereo outputs over USB, plus the analog outputs of the Virus.</li>
        <li>Audio from your DAW through the filters and effects of the Virus (sidechain input), as in the original.</li>
        <li>A help mode: the ? button explains every control, and why a greyed-out one is inactive.</li>
        <li>Projects bring back every patch setting of all 16 parts.</li>
        <li>VST3 on Windows, macOS and Linux; Audio Unit for Logic and GarageBand. Native on Apple Silicon.</li>
        <li>Editor sizes from 100 to 200 %. On larger sizes and Retina screens each image is enlarged once with a
        sharp filter, in memory only.</li>
      </ul>
    </div>

    <div class="feature">
      <div><h3>Help mode</h3>
      <p>Click the ? at the bottom right of the editor: every control gets a short note on what it does. Point at a
      control to read it, click to keep the note on screen; clicks do not change values in this mode, and the page tabs
      still work. Esc or ? ends it. The notes are written for TI Revive, not taken from the Access manual. A greyed-out
      control says why, for example &ldquo;Inactive now: Chorus Type is Off.&rdquo; or &ldquo;depends on Input Mode,
      Input Atomizer.&rdquo;</p></div>
      <figure class="shot"><img src="plugin-help-mode.png" width="1360" height="769"
      alt="The plug-in's help mode: a note explains the sub oscillator knob">
      <figcaption>Help mode, drawn from your own Access installer. TI Revive is not affiliated with Access Music or
      Kemper.</figcaption></figure>
      <figure class="shot"><img src="plugin-help-inactive.png" width="907" height="513"
      alt="A help note on the greyed-out chorus delay knob: Inactive now, Chorus Type is Off">
      <figcaption>Help note on a greyed-out control, drawn from your own Access installer. TI Revive is not affiliated
      with Access Music or Kemper.</figcaption></figure>
    </div>

    <div class="feature">
      <div><h3>Timing</h3>
      <p>The plug-in finds the clock of the Virus on USB, stamps every note against it and reports its latency, so the
      DAW lines recordings up with the grid. Any host sample rate works: a resampler converts to the 44.1 kHz of the Virus
      with -90 dB error.</p></div>
      <div><table class="data">
        <thead><tr><th>16th notes at 120 BPM</th><th>On the grid</th><th>Jitter</th><th>Drift</th></tr></thead>
        <tbody>
          <tr><th>Linux, REAPER</th><td>256 / 256</td><td>0.01 ms</td><td>0 ppm</td></tr>
          <tr><th>macOS, Ableton 12 (M3)</th><td>256 / 256</td><td>0.02 ms</td><td>0 ppm</td></tr>
          <tr><th>Windows 11, Ableton 11</th><td>304 / 304</td><td>&le; 0.66 ms*</td><td>0 ppm</td></tr>
          <tr class="old"><th>Classic plug-in 0.7.x</th><td>first late, second missing</td><td>0.06 ms</td><td>n/a</td></tr>
        </tbody>
      </table>
      <p class="small">Recorded from the USB audio of the Virus and measured by onset analysis. *The upper bound of the
      method on that machine.</p></div>
    </div>

    <div class="feature">
      <div><h3>CPU and audio thread</h3>
      <p>The audio thread never allocates memory or waits for a lock; note-offs always get a slot, so no note
      hangs.</p></div>
      <div><table class="data">
        <thead><tr><th></th><th>Before (0.7.x)</th><th>Now</th></tr></thead>
        <tbody>
          <tr><th>Idle, Virus connected</th><td>5 %</td><td>1.4 %</td></tr>
          <tr><th>Stress test: 16 channels of notes + 32 automated parameters</th><td>19 %</td><td>2.9 %</td></tr>
          <tr><th>One audio block, 3 LFOs automating the Virus for 40 min</th><td>n/a</td><td>max 122 &micro;s, 0 dropouts</td></tr>
        </tbody>
      </table>
      <p class="small">Checked with ThreadSanitizer and AddressSanitizer; {TESTS} unit tests run with every release build on Windows and
      Linux.</p></div>
    </div>

    <div class="feature">
      <div><h3>Editor speed</h3>
      <p>Only the controls that change are redrawn, the backgrounds of recent pages are cached, and on Linux the
      images live on the X server.</p></div>
      <table class="data">
        <thead><tr><th></th><th>Before</th><th>Now</th></tr></thead>
        <tbody>
          <tr><th>Full repaint on Linux</th><td>~70 ms</td><td>1&ndash;7 ms</td></tr>
          <tr><th>One knob step</th><td>~5 ms</td><td>&lt; 1 ms</td></tr>
          <tr><th>First visit of a page</th><td>27&ndash;52 ms</td><td>10&ndash;13 ms</td></tr>
        </tbody>
      </table>
    </div>

    <div class="feature">
      <div><h3>Problems found on the hardware</h3>
      <p>Behaviour of the Virus that is not documented anywhere, found by testing.</p></div>
      <ul>
        <li>A flood of parameter changes can hang the audio of the Virus until it is switched off. Automation is limited to
        250 messages per second and merged per parameter; a 4-minute full-load test passes.</li>
        <li>The Virus drops the message right after a multi dump. The plug-in pauses after one, so new projects reset
        the synth reliably.</li>
        <li>Bank sync requests make the Virus play its notes late. The sync pauses while you play and continues in the gaps.</li>
        <li>Switch the Virus off and on, or replug it, while the DAW runs: the editor shows the original's &ldquo;No Virus
        TI can be found&rdquo; page meanwhile, then the plug-in waits until the Virus is ready and plays in time again.
        Two DAWs share it: one plays, the other waits and takes over. Linux: after a cable replug, or when the Virus is
        switched on while no DAW runs, it can come back without sound; switch it off and on once while the DAW runs
        (being worked on).</li>
      </ul>
    </div>

    <div class="feature cc">
      <div><h3>TI Revive Control Center</h3>
      <p>A separate app in the look of the Access Virus Control Center, when that is in your Access installer. It
      shows the connected Virus and its RAM banks, backs them up as patch libraries and converts libraries between
      <code>.mid</code> and <code>.syx</code>. It only reads from the Virus.</p></div>
      <figure class="shot"><img src="control-center.png" width="687" height="463"
      alt="TI Revive Control Center in the look of Access' Virus Control Center">
      <figcaption>The Control Center in the Access look, drawn from your own installer. The Burn To Flash ROM and OS
      Updater pages are only kept for the look: they say that TI Revive does not offer these functions. TI Revive is
      not affiliated with Access Music or Kemper.</figcaption></figure>
    </div>

    <div class="feature cc">
      <div><h3>Nerd mode</h3>
      <p>Shows live what goes between TI Revive and the Virus, decoded: notes, parameter changes by name, dumps, the
      bank sync. It lists which programs use the Virus, shows the plug-in's and the broker's logs, and saves them all
      for a bug report in one file. It only watches; it never sends anything to the Virus.</p></div>
      <figure class="shot"><img src="control-center-nerd-mode.png" width="860" height="620"
      alt="Nerd mode: the plug-in plays notes and checks its bank cache, decoded live">
      <figcaption>The plug-in plays notes and checks its bank cache, decoded live.</figcaption></figure>
    </div>

    <div class="feature">
      <div><h3>What TI Revive will not do</h3>
      <p>On purpose.</p></div>
      <ul>
        <li>No firmware or OS updates for the Virus, no &ldquo;Burn To Flash&rdquo;, no factory reset. Anything that writes to the flash memory of the Virus can brick it.</li>
        <li>No Access files in TI Revive: the skin, the parameter descriptions and the ROM banks come from the installer
        you own.</li>
        <li>No telemetry: TI Revive never connects to the internet. Its parts talk to each other on your computer only,
        and the logs stay there.</li>
      </ul>
    </div>
  </div>
</section>
"""


def page_changelog_landing(releases):
    rows = []
    for r in releases:
        v = r["tag_name"]
        t = title_of(r)
        tag = '<span class="pre">pre-release</span>' if r.get("prerelease") else ""
        rows.append(f"""
    <article class="rel" id="{html.escape(v)}">
      <div><div class="v"><a href="#{html.escape(v)}">{html.escape(v[1:])}</a></div>
      <time datetime="{r["published_at"][:10]}">{date(r["published_at"])}</time>{tag}</div>
      <div>{f"<h3>{html.escape(t[:1].upper() + t[1:])}</h3>" if t else ""}<div class="body">{md(summary(r))}</div>
      <p class="more"><a href="{r["html_url"]}">Files and full notes</a></p></div>
    </article>""")
    return f"""
<section class="log">
  <div class="wrap">
    <h2>Every version</h2>
    <p class="intro">Newest first. Versions before 0.8.0 ran Access' original plug-in through an emulated driver;
    0.8.0 replaced it with TI Revive's own plug-in.</p>
    <div class="news">{"".join(rows)}
    </div>
  </div>
</section>
"""


# the README's screenshots that the site shows too (copied at build time)
SHARED_IMAGES = ["control-center.png", "control-center-nerd-mode.png", "plugin-help-mode.png", "plugin-help-inactive.png"]
TESTS = 139 # unit tests in native/tests (vtitests), as of 2026-10-17 (round 0.8.20)
# line coverage of vticore, measured like CI (no Access files, empty home): gcov over the test run
COVERAGE, COVERAGE_DATE = "94 %", "16 Oct 2026"

# the site: one page plus the changelog archive
PAGES = [("index", "TI Revive", lambda latest, releases, sums: page_landing(latest, releases, sums)),
         ("changelog", "TI Revive changelog", lambda latest, releases, sums: page_changelog_landing(releases))]


def render(out, releases, sums):
    src = os.path.join(ROOT, "site", "src")
    with open(os.path.join(src, "base.html")) as f:
        base = f.read()
    os.makedirs(out, exist_ok=True)
    for name in os.listdir(src):
        if name != "base.html" and os.path.isfile(os.path.join(src, name)):
            shutil.copy(os.path.join(src, name), os.path.join(out, name))
    for img in SHARED_IMAGES:
        shutil.copy(os.path.join(ROOT, "docs", "images", img), os.path.join(out, img))
    latest = next(r for r in releases if not r.get("prerelease") and not r.get("draft"))
    for slug, title, fn in PAGES:
        page = (base.replace("{{title}}", title).replace("{{body}}", fn(latest, releases, sums))
                .replace("{{version}}", latest["tag_name"][1:]).replace("{{date}}", date(latest["published_at"]))
                .replace("{{repo}}", GH))
        with open(os.path.join(out, slug + ".html"), "w") as f:
            f.write(page)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--releases")
    ap.add_argument("--checksums", action="store_true")
    ap.add_argument("--cache", default=os.path.join(ROOT, "site", ".cache"))
    ap.add_argument("--out", default=os.path.join(ROOT, "site", "dist"))
    a = ap.parse_args()
    releases = sorted(load_releases(a.releases), key=lambda r: r["published_at"], reverse=True)
    latest = next(r for r in releases if not r.get("prerelease") and not r.get("draft"))
    sums = sha256_of_assets(latest, a.cache) if a.checksums else {}
    render(a.out, releases, sums)
    print("built ->", a.out, "(%d releases, latest %s, checksums %s)" % (len(releases), latest["tag_name"],
          "yes" if sums else "no"))


if __name__ == "__main__":
    sys.exit(main())
