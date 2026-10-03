#!/bin/bash
# Download the GUITION-DISK sharded zip, verify SHA256 per shard, concatenate.
set -u
cd "$(dirname "$0")"
MAN=manifest.json
OUT=JC8012P4A1C_I_W_Y.zip
python -c "
import json,sys
m=json.load(open('$MAN',encoding='utf-8'))
with open('shards/list.txt','w',newline='\n') as f:
    for s in sorted(m['shards'],key=lambda s:s['seq']):
        f.write('%d %d %s %s\n'%(s['seq'],s['size'],s['sha256'],s['url']))
"
echo "list.txt lines: $(wc -l < shards/list.txt)"
fail=0
while read seq size sha url; do
  f="shards/shard_$seq.bin"
  for attempt in 1 2 3 4; do
    if [ -f "$f" ] && [ "$(stat -c %s "$f")" = "$size" ]; then
      got=$(sha256sum "$f" | cut -d' ' -f1)
      if [ "$got" = "$sha" ]; then echo "shard $seq OK ($size bytes, sha256 verified)"; break; fi
      echo "shard $seq sha mismatch, re-downloading"; rm -f "$f"
    fi
    echo "shard $seq attempt $attempt: $url"
    curl -s -L --retry 3 --retry-delay 5 --max-time 1500 -H 'X-Gdisk-Fetch: 1' -o "$f" "$url"
    echo "  curl exit=$? size=$(stat -c %s "$f" 2>/dev/null)"
  done
  if [ ! -f "$f" ] || [ "$(sha256sum "$f" | cut -d' ' -f1)" != "$sha" ]; then echo "shard $seq FAILED"; fail=1; fi
done < shards/list.txt
if [ $fail = 0 ]; then
  rm -f "$OUT"
  for seq in $(cut -d' ' -f1 shards/list.txt); do cat "shards/shard_$seq.bin" >> "$OUT"; done
  echo "assembled $OUT size=$(stat -c %s "$OUT")"
  echo "magic: $(head -c 4 "$OUT" | xxd -p)"
  echo "sha256 whole: $(sha256sum "$OUT" | cut -d' ' -f1)"
  python -c "import zipfile;z=zipfile.ZipFile('$OUT');print('zip entries',len(z.namelist()));print('testzip ->',z.testzip())"
else
  echo "DOWNLOAD INCOMPLETE"
fi
