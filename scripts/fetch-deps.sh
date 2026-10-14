#!/bin/sh
set -eu
mkdir -p dependencies
if [ ! -d dependencies/nx-player/.git ]; then
  git clone https://github.com/shodowlo/NX-torrent-player.git dependencies/nx-player
fi
git -C dependencies/nx-player checkout --detach 38ef229656efe548098a11ddb4b83d258645de63
if [ ! -d dependencies/quickjs/.git ]; then
  git clone https://github.com/bellard/quickjs.git dependencies/quickjs
fi
git -C dependencies/quickjs checkout --detach a37f62afbb6ad245cd84beb297fad00bef2c5410
python3 scripts/patch-quickjs.py

python3 scripts/force-chinese-ui.py
