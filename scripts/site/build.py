#!/usr/bin/env python3
# build.py - the TI Revive website (GitHub Pages) from the GitHub releases and the texts below.
# Static pages, no JavaScript, no external fonts or trackers. Each design in site/src/<design>/ has a page frame
# (base.html) and a style sheet; the content is the same for all of them.
#
#   python3 scripts/site/build.py [--releases FILE] [--checksums] [--design NAME ...] [--out DIR]
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
              "Virus Control and runs natively on Windows, macOS (Apple Silicon and Intel) and Linux - no Rosetta, "
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


# ------------------------------------------------------------------------------------------------ the pages
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


def page_home(latest):
    v = latest["tag_name"][1:]
    return f"""
<section class="lead">
  <h1>TI Revive</h1>
  <p class="claim">Total Integration for the Access Virus TI on Windows 11, macOS and Linux &mdash; without the old
  drivers.</p>
  <p>The Virus TI was built to live inside your DAW: its plug-in edits every parameter, the synth's audio comes back
  over the same USB cable, and the timing is locked to the host. The drivers that made this work stopped working on
  current systems. TI Revive replaces them.</p>
  <p>It comes with <strong>TI Revive Virus Control</strong>, a plug-in written from scratch that looks and works like
  Access' original &mdash; parameter sync, preset browser, patch libraries, automation, the Virus audio in your DAW
  &mdash; and runs natively, without Rosetta, Wine or yabridge.</p>
  <p class="actions"><a class="button" href="download.html">Download {v}</a>
  <a href="changelog.html">What changed</a></p>
</section>

<section>
  <h2>What you need</h2>
  <table class="facts">
    <tr><th>Synth</th><td>Access Virus TI, TI2 (Desktop, Polar, Keyboard). The TI Snow works according to the first
    user reports; we have none to test with.</td></tr>
    <tr><th>Computer</th><td>Windows 11 x64 (also 10) &middot; macOS 14.4 or newer, Apple Silicon or Intel &middot;
    Linux x86_64 with systemd (Ubuntu 22.04 / Debian 12 or newer)</td></tr>
    <tr><th>Access files</th><td>Your own &ldquo;Virus TI Software Suite&rdquo; installer (e.g. 5.1.7). TI Revive only
    unpacks the parts the plug-in reads &mdash; the skin, the parameter descriptions, the ROM banks. It contains no
    Access material itself.</td></tr>
    <tr><th>DAW</th><td>Any VST3 host; Audio Unit on macOS (Logic, GarageBand). Tested with Ableton Live 11 and 12,
    REAPER and GarageBand.</td></tr>
  </table>
</section>

<section>
  <h2>In practice</h2>
  <dl class="notes">
    <dt>Timing</dt><dd>Notes land on the grid within a tenth of a millisecond (measured on Linux and macOS), and the
    plug-in reports its latency so the DAW compensates it. Any host sample rate works.</dd>
    <dt>First start</dt><dd>The plug-in reads RAM A&ndash;D from the Virus once, which takes a few minutes; you can play
    while it runs. After that it takes seconds.</dd>
    <dt>Outputs</dt><dd>USB 1 and 2 by default, USB 3 if you switch it on (PATCH UTILITY &rarr; Preferences). Windows
    also gets an ASIO device, &ldquo;TI Revive Virus Audio&rdquo;, to use the Virus as a sound card.</dd>
    <dt>Not included</dt><dd>Firmware or OS updates for the Virus and anything that writes its flash. On purpose.</dd>
  </dl>
</section>

<section>
  <h2>Known limits</h2>
  <ul>
    <li>The Virus' USB audio <em>input</em> (&ldquo;3 Outs / 1 Input&rdquo;) is not fed by the plug-in yet; its
    outputs work.</li>
    <li>Snow, Polar and Keyboard are not tested by us &mdash; we have a TI2 Desktop. Reports are welcome.</li>
    <li>The installers are not signed, so Windows and macOS ask once before they run them.</li>
  </ul>
</section>
"""


def page_download(latest, sums):
    v = latest["tag_name"][1:]
    win = asset(latest, "-setup.exe")
    msi = asset(latest, ".msi")
    mac = asset(latest, "-mac.zip")
    lin = asset(latest, "-linux.tar.gz")

    def link(a):
        if not a:
            return ""
        s = sums.get(a["name"])
        return (f'<a class="file" href="{a["browser_download_url"]}">{a["name"]}</a> '
                f'<span class="size">{size(a["size"])}</span>' +
                (f'<br><code class="sum" title="SHA-256">{s}</code>' if s else ""))

    return f"""
<section class="lead">
  <h1>Download</h1>
  <p>Version <strong>{v}</strong>, released {date(latest["published_at"])}. Free, and it stays free.
  <a href="changelog.html#v{v}">What is new in {v}</a></p>
</section>

<section class="platform" id="windows">
  <h2>Windows 11 <span class="sub">x64, also Windows 10</span></h2>
  <p class="files">{link(win)}<br>{link(msi)}</p>
  <ol>
    <li>Put the setup file and your Access &ldquo;Virus TI Software Suite&rdquo; 64-bit <code>.msi</code> in the
    same folder. The <code>.msi</code> of TI Revive is the same installer without the wrapper.</li>
    <li>Run it. It is not code-signed, so Windows may say &ldquo;Windows protected your PC&rdquo;: <em>More info
    &rarr; Run anyway</em>. Keep &ldquo;Set up the Access Virus TI files now&rdquo; checked &mdash; your Access
    installer is only unpacked, not installed.</li>
    <li>Connect the Virus, rescan plug-ins and use <strong>TI Revive Virus Control</strong>. For the Virus as a sound
    card choose the ASIO device <strong>TI Revive Virus Audio</strong>.</li>
  </ol>
</section>

<section class="platform" id="macos">
  <h2>macOS <span class="sub">14.4 or newer, Apple Silicon and Intel</span></h2>
  <p class="files">{link(mac)}</p>
  <ol>
    <li>Unpack the zip and open <code>TI Revive.pkg</code>. It is not signed: right-click &rarr; <em>Open</em>, or
    <em>System Settings &rarr; Privacy &amp; Security &rarr; Open Anyway</em>.</li>
    <li>Put your Access <code>.pkg</code> next to <code>Set up Access Virus TI files.command</code> and run it. It
    unpacks only what the plug-in reads and parks old drivers that would block the Virus.</li>
    <li>Start your DAW and allow it to use the microphone when macOS asks &mdash; macOS gives the Virus' audio only to
    apps with that permission. Use <strong>TI Revive Virus Control</strong> as VST3 or Audio Unit.</li>
  </ol>
</section>

<section class="platform" id="linux">
  <h2>Linux <span class="sub">x86_64, systemd</span></h2>
  <p class="files">{link(lin)}</p>
  <ol>
    <li>Install <code>msitools</code> (<code>sudo apt install msitools</code>) to unpack the Access installer.</li>
    <li>Unpack the archive and run <code>./install.sh "/path/to/Virus TI Software Suite 64-bit.msi"</code>. It asks
    for your password once (udev rule) and puts the plug-in into <code>~/.vst3</code>.</li>
    <li>For steady audio add your user to the real-time group (Ubuntu: <code>sudo usermod -aG pipewire $USER</code>,
    then log out and in), rescan plug-ins and use <strong>TI Revive Virus Control</strong>.</li>
  </ol>
</section>

<section>
  <h2>Older versions</h2>
  <p>Every release with its files is on <a href="{GH}/releases">GitHub</a>. Coming from 0.7.x: the classic plug-in
  &ldquo;Virus TI (TI Revive)&rdquo; is replaced, its projects open with the new one.</p>
</section>
"""


def page_changelog(releases):
    rows = []
    for r in releases:
        v = r["tag_name"]
        t = title_of(r)
        tag = '<span class="pre">pre-release</span>' if r.get("prerelease") else ""
        rows.append(f"""
<article class="release" id="{html.escape(v)}">
  <header><h2><a href="#{html.escape(v)}">{html.escape(v[1:])}</a>{(" &mdash; " + html.escape(t)) if t else ""}</h2>
  <p class="meta"><time datetime="{r["published_at"][:10]}">{date(r["published_at"])}</time> {tag}
  <a href="{r["html_url"]}">files</a></p></header>
  {md(summary(r))}
</article>""")
    return f"""
<section class="lead">
  <h1>Changelog</h1>
  <p>What changed in each version, newest first. Versions before 0.8.0 ran Access' original plug-in through an
  emulated driver; 0.8.0 replaced it with TI Revive's own plug-in.</p>
</section>
{"".join(rows)}
"""


def page_help():
    return f"""
<section class="lead">
  <h1>Help</h1>
  <p>The usual problems and what to do. If yours is not here, open an
  <a href="{GH}/issues">issue</a> and attach the logs (see below).</p>
</section>

<section class="faq">
  <h2 id="not-connected">&ldquo;Virus TI not connected&rdquo;</h2>
  <p>Only one driver can use the Virus' USB interfaces. Old Access drivers from a full Access install, or third-party
  ones like the USB driver of Aura's Virus Editor, take them first. Disable the other driver and restart. On macOS the
  setup script offers to park the known ones.</p>

  <h2 id="silent">Connected, but no sound</h2>
  <p>The Virus decides per part where its sound goes. If another editor changed that, switch the part's output to
  &ldquo;Out 1&rdquo; and back to &ldquo;USB 1&rdquo; in the plug-in &mdash; that sends the setting again. On macOS
  also check that your DAW may use the microphone.</p>

  <h2 id="usb3">Three separate outputs (USB 3)</h2>
  <p>In the plug-in, open PATCH UTILITY &rarr; Preferences and set &ldquo;Input and Output Configuration&rdquo; to
  &ldquo;3 Outs / No Input&rdquo;. Then a part's output can be &ldquo;USB3 L+R&rdquo;, and its sound comes out of the
  plug-in's Out 3. In Ableton: an audio track with <em>Audio From</em> = the Virus track, <em>Out 3</em>,
  <em>Monitor In</em>.</p>

  <h2 id="first-notes">The first notes after Play are late</h2>
  <p>The plug-in sends notes slightly ahead to make up for the USB round trip, so start playback a little before the
  first note. The original driver behaved the same.</p>

  <h2 id="two-daws">Two DAWs, or a duplicated track</h2>
  <p>One plug-in uses the Virus at a time; the others show &ldquo;Virus unavailable&rdquo; and take over when it is
  removed or its DAW closes.</p>

  <h2 id="logs">Logs for a bug report</h2>
  <p>Create an empty file <code>diag.on</code> in the plug-in's folder, reproduce the problem, and attach
  <code>diag.log</code> from the same folder:</p>
  <table class="facts">
    <tr><th>Windows</th><td><code>%LOCALAPPDATA%\\virus-ti-native</code></td></tr>
    <tr><th>macOS</th><td><code>~/Library/Application Support/virus-ti-native</code></td></tr>
    <tr><th>Linux</th><td><code>~/.local/share/virus-ti-native</code></td></tr>
  </table>
  <p>The Control Center's Nerd mode collects the same logs, without the home folder or user name.</p>
</section>
"""


def page_about():
    return f"""
<section class="lead">
  <h1>About</h1>
  <p>TI Revive is a free, open-source project. It started because a Virus TI deserved to keep working on the
  computers people use today.</p>
</section>

<section>
  <h2>Not Access</h2>
  <p>TI Revive is not affiliated with, endorsed by or connected to Access Music or Kemper. &ldquo;Access&rdquo; and
  &ldquo;Virus&rdquo; are their names. TI Revive contains no Access software, skins, firmware or patches; the plug-in
  reads them at runtime from the installer you own.</p>

  <h2>How it was built</h2>
  <p>By reverse engineering the USB protocol and the original software's behaviour from logs, captures and static
  analysis, and by testing on real hardware. Every claim about the device is written down with its source in the
  repository. Much of the work was done with an AI coding assistant; the tests on the synth were done by hand.</p>

  <h2>Taking part</h2>
  <p>Test reports for the Snow, Polar and Keyboard, other DAWs and systems help most &mdash; no coding needed. See the
  <a href="{GH}#readme">README</a> and the <a href="{GH}/issues/new?template=test_report.yml">test report form</a>.
  Source code and licence: <a href="{GH}">github.com/{REPO}</a>.</p>

  <h2>Saying thanks</h2>
  <p>Everything works the same whether you pay or not. If TI Revive gave your Virus a second life and you want to,
  you can <a href="https://buymeacoffee.com/callmederpsn">buy the developer a coffee</a>. A test report or a kind
  word helps just as much.</p>
</section>
"""


# ------------------------------------------------------------------------------------- the one-page design
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
                         "Run it. It is not code-signed: <em>More info &rarr; Run anyway</em>. Your Access installer "
                         "is only unpacked, not installed.",
                         "Connect the Virus, rescan plug-ins in your DAW and load <strong>TI Revive Virus "
                         "Control</strong>. The ASIO device <strong>TI Revive Virus Audio</strong> makes the Virus a "
                         "sound card."])
    mac = os_card("macOS", "macOS 14.4 or newer, Apple Silicon and Intel", asset(latest, "-mac.zip"), None, sums,
                  ["Unpack the zip and open <code>TI Revive.pkg</code> (not signed: right-click &rarr; "
                   "<em>Open</em>).",
                   "Put your Access <code>.pkg</code> next to <code>Set up Access Virus TI files.command</code> and "
                   "run it. It also parks old drivers that would block the Virus.",
                   "Start your DAW and allow microphone access when asked &mdash; macOS hands the Virus' audio only "
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
    <h1>TI Revive</h1>
    <p class="tagline">Like meeting an old best friend again.</p>
    <p class="lead"><strong>Total Integration for the Access Virus TI on Windows 11, macOS and Linux &mdash; without
    the old drivers.</strong> Its plug-in looks and works like the original Virus Control: every parameter, the preset
    browser, automation, and the synth's audio over the same USB cable.</p>
    <p class="actions"><a class="button" href="#download">Download {v}</a>
    <a class="quiet" href="#news">What's new &rarr;</a></p>
    <p class="facts-line">Free &middot; open source &middot; VST3 and Audio Unit &middot; released {when}</p>
    <p class="thanks">&#9749; <strong>Free, and it will stay free.</strong> If TI Revive gives your Virus a second life
    and you'd like to say thanks, you can <a href="https://buymeacoffee.com/callmederpsn">buy me a coffee</a> -
    entirely optional, every feature works the same either way. A test report or a kind word helps just as much.</p>
    <figure class="shot">
      <img src="plugin-filter.png" width="1280" height="724"
      alt="TI Revive Virus Control, the plug-in's filter page, drawn from the user's own Access files">
      <figcaption>TI Revive Virus Control at 200 %. The look comes from your own Access installer at runtime; TI
      Revive is not affiliated with Access Music or Kemper and contains no Access files.</figcaption>
    </figure>
  </div>
</section>

<section class="block" id="download">
  <div class="wrap">
    <h2>Version {v}, released {when}</h2>
    <p class="intro">You also need your own Access &ldquo;Virus TI Software Suite&rdquo; installer (e.g. 5.1.7).
    TI Revive unpacks only what the plug-in reads from it &mdash; the skin, the parameter descriptions, the ROM banks.
    Older versions are on <a href="{GH}/releases">GitHub</a>.</p>
    <div class="oses">{win}{mac}{lin}
    </div>
    <div class="needs">
      <div><b>Synth</b><span>Virus TI and TI2: Desktop, Polar, Keyboard, and the TI Snow</span></div>
      <div><b>DAW</b><span>Any VST3 host; Audio Unit for Logic and GarageBand</span></div>
      <div><b>Outputs</b><span>USB 1 and 2, USB 3 if you switch it on in PATCH UTILITY &rarr; Preferences</span></div>
      <div><b>Not included</b><span>Firmware or OS updates for the Virus, or anything else that writes its flash.
      On purpose.</span></div>
    </div>
  </div>
</section>

<section class="block" id="how">
  <div class="wrap">
    <h2>Three steps, then it stays out of the way</h2>
    <p class="intro">The plug-in talks to the Virus directly over USB, so there is no MIDI port to set up and no
    driver of Access' left on your system.</p>
    <div class="steps">
      <div><h3>Install TI Revive</h3><p>One installer per system. It sets up the USB access and puts the plug-in where
      your DAW looks for it.</p></div>
      <div><h3>Point it at your Access installer</h3><p>The setup unpacks the skin, the parameter descriptions and the
      ROM banks from the installer you own. Nothing of Access' comes with TI Revive.</p></div>
      <div><h3>Load the plug-in</h3><p><strong>TI Revive Virus Control</strong> on a MIDI track. Projects made with the
      original plug-in open with it.</p></div>
    </div>
    <div class="notes">
      <div><h3>Timing</h3><p>Notes land on the grid within a tenth of a millisecond, and the plug-in reports its latency
      so the DAW compensates it. Any host sample rate works.</p></div>
      <div><h3>First start</h3><p>The plug-in reads RAM A&ndash;D from the Virus once, which takes a few minutes; you can
      play while it runs. After that it takes seconds.</p></div>
      <div><h3>Two programs</h3><p>One plug-in uses the Virus at a time; another DAW shows &ldquo;Virus
      unavailable&rdquo; and takes over when the first one closes.</p></div>
      <div><h3>Sharp on large screens</h3><p>At larger editor sizes and on Retina screens the original images are
      enlarged once with a sharp filter, in memory only.</p></div>
    </div>
  </div>
</section>

<section class="block" id="status">
  <div class="wrap">
    <h2>What is tested where</h2>
    <p class="intro">We test on a Virus TI2 Desktop. Everything else depends on reports from people who own one
    &mdash; a <a href="{GH}/issues/new?template=test_report.yml">test report</a> helps more than anything.</p>
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
      <li>The Virus' USB audio <em>input</em> (&ldquo;3 Outs / 1 Input&rdquo;) is not fed by the plug-in yet.</li>
      <li>The installers are not signed, so Windows and macOS ask once before they run them.</li>
    </ul>
  </div>
</section>

<section class="block" id="news">
  <div class="wrap">
    <h2>Recent releases</h2>
    <div class="news">{"".join(news)}
    </div>
    <p class="more"><a href="changelog.html">Every version &rarr;</a>
    <a href="{GH}/releases.atom">Release feed (Atom)</a>
    <a href="{GH}">On GitHub: <em>Watch &rarr; Custom &rarr; Releases</em></a></p>
  </div>
</section>

<section class="block" id="help">
  <div class="wrap">
    <h2>Common questions</h2>
    <div class="faq">
      <details><summary>&ldquo;Virus TI not connected&rdquo;</summary>
      <p>Only one driver can use the Virus' USB interfaces. Old Access drivers, or third-party ones like the driver of
      another Virus editor, take them first. Disable the other driver and restart; on macOS the setup script offers to
      park the known ones.</p></details>
      <details><summary>Connected, but no sound</summary>
      <p>The Virus decides per part where its sound goes. Switch the part's output to &ldquo;Out 1&rdquo; and back to
      &ldquo;USB 1&rdquo; in the plug-in to send the setting again. On macOS also check that your DAW may use the
      microphone.</p></details>
      <details><summary>Three separate outputs (USB 3)</summary>
      <p>PATCH UTILITY &rarr; Preferences &rarr; &ldquo;Input and Output Configuration&rdquo; = &ldquo;3 Outs / No
      Input&rdquo;. Then a part's output can be &ldquo;USB3 L+R&rdquo; and its sound comes out of the plug-in's Out 3.
      In Ableton: an audio track with <em>Audio From</em> = the Virus track, <em>Out 3</em>, <em>Monitor In</em>.</p>
      </details>
      <details><summary>The first notes after Play are late</summary>
      <p>The plug-in sends notes slightly ahead to make up for the USB round trip, so start playback a little before
      the first note. The original driver did the same.</p></details>
      <details><summary>Logs for a bug report</summary>
      <p>Create an empty file <code>diag.on</code> in the plug-in's folder, reproduce the problem and attach
      <code>diag.log</code> from the same folder to an <a href="{GH}/issues">issue</a>:</p>
      <div class="paths"><b>Windows</b><code>%LOCALAPPDATA%\\virus-ti-native</code>
      <b>macOS</b><code>~/Library/Application Support/virus-ti-native</code>
      <b>Linux</b><code>~/.local/share/virus-ti-native</code></div></details>
    </div>
  </div>
</section>

<section class="block" id="about">
  <div class="wrap">
    <div class="about">
      <div><h3>Why</h3><p>A Virus TI deserves to keep working on the computers people use today. TI Revive is free and
      open source, and it stays that way.</p></div>
      <div><h3>How it was built</h3><p>By working out the USB protocol and the original software's behaviour from
      logs, captures and analysis, and by testing on real hardware. Much of the work was done with an AI coding
      assistant; the tests on the synth are done by hand.</p></div>
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


PAGES = [("index", "Home", page_home), ("download", "Download", page_download), ("changelog", "Changelog",
         page_changelog), ("help", "Help", page_help), ("about", "About", page_about)]


# designs with their own set of pages (the others use PAGES)
LAYOUTS = {"landing": [("index", "Home", lambda latest, releases, sums: page_landing(latest, releases, sums)),
                       ("changelog", "Changelog", lambda latest, releases, sums: page_changelog_landing(releases))]}


def render(design, out, releases, sums):
    src = os.path.join(ROOT, "site", "src", design)
    with open(os.path.join(src, "base.html")) as f:
        base = f.read()
    os.makedirs(out, exist_ok=True)
    for name in os.listdir(src):
        if name != "base.html":
            shutil.copy(os.path.join(src, name), os.path.join(out, name))
    latest = next(r for r in releases if not r.get("prerelease") and not r.get("draft"))
    pages = LAYOUTS.get(design, PAGES)
    for slug, label, fn in pages:
        if design in LAYOUTS:
            body = fn(latest, releases, sums)
        else:
            body = {"index": lambda: fn(latest), "download": lambda: fn(latest, sums),
                    "changelog": lambda: fn(releases)}.get(slug, fn)()
        nav = "\n".join(f'<a href="{s}.html"{" aria-current=\"page\"" if s == slug else ""}>{lab}</a>'
                        for s, lab, _ in pages)
        page = (base.replace("{{title}}", "TI Revive" if slug == "index" else label + " - TI Revive")
                .replace("{{nav}}", nav).replace("{{body}}", body).replace("{{version}}", latest["tag_name"][1:])
                .replace("{{date}}", date(latest["published_at"])).replace("{{repo}}", GH))
        with open(os.path.join(out, slug + ".html"), "w") as f:
            f.write(page)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--releases")
    ap.add_argument("--checksums", action="store_true")
    ap.add_argument("--cache", default=os.path.join(ROOT, "site", ".cache"))
    ap.add_argument("--design", action="append")
    ap.add_argument("--out", default=os.path.join(ROOT, "site", "dist"))
    a = ap.parse_args()
    releases = sorted(load_releases(a.releases), key=lambda r: r["published_at"], reverse=True)
    latest = next(r for r in releases if not r.get("prerelease") and not r.get("draft"))
    sums = sha256_of_assets(latest, a.cache) if a.checksums else {}
    designs = a.design or sorted(d for d in os.listdir(os.path.join(ROOT, "site", "src"))
                                 if os.path.isdir(os.path.join(ROOT, "site", "src", d)))
    for d in designs:
        render(d, os.path.join(a.out, d) if len(designs) > 1 else a.out, releases, sums)
    if len(designs) > 1: # the drafts side by side
        links = "".join(f'<li><a href="{d}/index.html">{d}</a></li>' for d in designs)
        with open(os.path.join(a.out, "index.html"), "w") as f:
            f.write(f'<!doctype html><meta charset="utf-8"><title>TI Revive drafts</title>'
                    f'<style>body{{font:16px/1.5 system-ui;margin:3em;max-width:30em}}</style>'
                    f'<h1>TI Revive - website drafts</h1><ul>{links}</ul>')
    print("built", ", ".join(designs), "->", a.out, "(%d releases, checksums %s)" % (len(releases),
          "yes" if sums else "no"))


if __name__ == "__main__":
    sys.exit(main())
