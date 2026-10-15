# Moving the native plug-in into TI Revive (only on the maintainer's explicit go)

The native plug-in moves into the public TI Revive repository as `native/`, with its full history. Everything
below was rehearsed end to end on 2026-10-14 (scratch copies, nothing published): filter, merge, submodules,
build and unit tests in the TI Revive layout, publication scans of the merged history.

## What is prepared

| where | what |
|---|---|
| `packaging/windows/native.wxs` | WiX fragment: VST3 bundle + ASIO driver (registry values), removes the test builds' `VirusTINative.vst3` and their ASIO entry |
| `packaging/ti-revive/*.patch` | TI Revive changes (local branch `native-integration` in the TI Revive working copy, not pushed): MSI (`build.ps1 -NativeBin`), Mac/Linux release scripts and installers, release workflow jobs, `.gitignore` allowlist |
| `CMakeLists.txt` | takes the broker headers from `../src/broker` when it lives inside TI Revive, else from the submodule |

Tested: the MSI with the native part in the win11 VM (install over 0.7.4, ASIO registered, uninstall/reinstall),
the Linux tarball (`scripts/linux-build-release.sh` with `native/`), installer step 5 with a temporary HOME, the Mac
pkg (CI `mac-package`, installed on the M3), and the **complete release workflow**: steps 2-3 below into the private
repository `derpsn/tirevive-rehearsal`, tag `v0.8.0` pushed there - all 7 jobs green, release with setup.exe, MSI,
Mac zip, Linux tarball (native plug-in needs glibc 2.35 / GLIBCXX 3.4.30) and usbinfo script (2026-10-14). The
rehearsal found and fixed: the Wine-side tools do not build with Ubuntu 22.04's mingw (patch 0008: own native-linux
job). The rehearsal repository can be deleted after the real release.

## Steps

1. Publication check (again, on the final state): no Access material, no decompiled code, no personal data -
   the scans below must print nothing / 0.
2. Filter a fresh clone of this repository (git-filter-repo 2.47):
   ```
   git clone --no-local <virus-ti-native> native-src && cd native-src
   printf '<local user name>==>user\n' > ../repl.txt   # the developer's user name in early tool scripts
   git filter-repo --invert-paths --path external/ti-revive --to-subdirectory-filter native \
       --replace-text ../repl.txt --force
   ```
   (`external/ti-revive` was this repo's submodule pointing at TI Revive itself - dropped from all history.)
3. In TI Revive (branch with the patches from `packaging/ti-revive/`, `git am`):
   ```
   git remote add native ../native-src && git fetch native
   git merge --allow-unrelated-histories native/main -m "Merge the native plug-in (virus-ti-native) into native/ with its history"
   git rm -r native/.github native/.gitmodules
   printf '[submodule "native/external/vst3sdk"]\n\tpath = native/external/vst3sdk\n\turl = https://github.com/steinbergmedia/vst3sdk.git\n' > .gitmodules
   git add .gitmodules && git commit -m "native/: vst3sdk submodule in the root .gitmodules; its own CI and .gitmodules removed"
   git submodule update --init --recursive native/external/vst3sdk
   ```
4. Build check: `cmake -S native -B build-native -DCMAKE_BUILD_TYPE=Release && cmake --build build-native && ctest --test-dir build-native`.
5. Scans of the merged history (expected: identities derpsn + the earlier external contributor, all counts 0):
   ```
   git log --all --format='%an <%ae>' | sort | uniq -c
   git rev-list --all | xargs -n 50 git grep -l -iE '<user name>|<real name>' | wc -l
   git rev-list --all | xargs -n 50 git grep -c -E 'uVar[0-9]|param_[0-9]|undefined[1248]|CONCAT[0-9]{2}' -- native | wc -l
   git log --all --pretty=format: --name-only --diff-filter=A -- native | sort -u   # only own source/doc types
   ```
6. Docs in TI Revive: README (the native plug-in as the main way, the wrapper as fallback), `findings/HANDOFF.md`,
   AGENTS.md map (`native/`), CONTRIBUTING (building native/).
7. Push the branch, merge to main (no force push), tag `v0.8.0` -> the release workflow builds MSI, Mac zip and
   Linux tarball with the native plug-in.
8. Afterwards: this private repository is archived (read-only); work continues in TI Revive.
