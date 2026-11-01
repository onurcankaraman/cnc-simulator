#!/usr/bin/env bash
# Lightweight, quiet, developer-side guard. Not a substitute for OS/container isolation.
set -euo pipefail
if ! command -v python3 >/dev/null 2>&1; then exit 0; fi
python3 -c '
import json,sys,re
try:
    data=json.load(sys.stdin)
    cmd=str(data.get("tool_input",{}).get("command", ""))
except Exception:
    sys.exit(0)
blocked=[r"\brm\s+-rf\s+/(?:\s|$)",r"\bmkfs(?:\.[a-z0-9]+)?\s+",r"\bdd\s+if=/dev/(?:zero|random)\s+of=/dev/",r"\bdocker\s+system\s+prune\s+--all\s+--volumes\b"]
if any(re.search(p,cmd) for p in blocked):
    print("Blocked destructive shell command. Require manual operator review.",file=sys.stderr)
    sys.exit(2)
sys.exit(0)
' 
