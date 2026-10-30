#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PS4_HOST="${PS4_HOST:-10.0.1.157}"
PS4_FTP_PORT="${PS4_FTP_PORT:-2121}"
REMOTE_DIR="${PS4_PKG_DIR:-${REMOTE_DIR:-/data/pkg}}"
PS4DEBUG_PYTHON="${PS4DEBUG_PYTHON:-python3}"

CONTENT_ID="IV0000-OGNM00001_00-OPENGNMHWSMOKE00"
PKG_PATH="${PKG_PATH:-$SCRIPT_DIR/$CONTENT_ID.pkg}"

if [[ ! -f "$PKG_PATH" ]]; then
    "$SCRIPT_DIR/build.sh" docker-hardware-pkg
fi

pkg_name="$(basename "$PKG_PATH")"
remote_url="ftp://$PS4_HOST:$PS4_FTP_PORT$REMOTE_DIR/$pkg_name"

curl --fail --silent --show-error --ftp-create-dirs -T "$PKG_PATH" "$remote_url"
curl --fail --silent --show-error "ftp://$PS4_HOST:$PS4_FTP_PORT$REMOTE_DIR/" | grep -F "$pkg_name"

if [[ -x "$PS4DEBUG_PYTHON" ]]; then
    "$PS4DEBUG_PYTHON" - "$PS4_HOST" "$REMOTE_DIR/$pkg_name" <<'PY'
import asyncio
import sys
from ps4debug import PS4Debug

async def main() -> None:
    host, path = sys.argv[1], sys.argv[2]
    await PS4Debug(host).notify(f"opengnm hardware smoke PKG staged: {path}")

asyncio.run(main())
PY
fi

cat <<EOF

Staged $pkg_name to ftp://$PS4_HOST:$PS4_FTP_PORT$REMOTE_DIR/

Install and launch title ID OGNM00001 on the PS4.
EOF
