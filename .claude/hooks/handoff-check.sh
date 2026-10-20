#!/usr/bin/env bash
# Claude Code Stop hook: a turn may not end while commits sit after the last commit that touched
# findings/HANDOFF.md, so the maintainers' working log never falls behind the work (resumable sessions).
# Uncommitted work does not count: mid-task turns end with a dirty tree all the time.
# Any failure (no git, no repo, no HANDOFF) allows the stop: a broken hook must never trap a session.
event=$(cat)

# Second stop after a block: allow it, else a refused update would loop forever.
if printf '%s' "$event" | grep -q '"stop_hook_active"[[:space:]]*:[[:space:]]*true'; then
    exit 0
fi

cd "${CLAUDE_PROJECT_DIR:-.}" 2>/dev/null || exit 0
last=$(git log -1 --format=%H -- findings/HANDOFF.md 2>/dev/null) || exit 0
[ -n "$last" ] || exit 0
count=$(git rev-list --count "$last..HEAD" 2>/dev/null) || exit 0
[ "$count" -gt 0 ] || exit 0

newest=$(git log -1 --format=%s HEAD)
[ "$count" -eq 1 ] && word=commit || word=commits
reason="$count $word since the last HANDOFF update (newest: $newest). Update findings/HANDOFF.md (state, results, NEXT) and commit it before ending the turn."
python3 -I -c 'import json, sys; print(json.dumps({"decision": "block", "reason": sys.argv[1]}))' "$reason"
