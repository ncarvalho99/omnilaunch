#!/usr/bin/env python3
"""Build romfs/data/gametdb.tsv: GameTDB Switch box-art ids by game name.

GameTDB (https://www.gametdb.com) publishes its Switch database for use by
homebrew launchers. The menu matches an installed game's name against this
index (art/ArtNames.cpp nameKey, which must fold names exactly as name_key
below) and fetches the box cover from art.gametdb.com.

Usage (offline; the database file is downloaded separately):

    python tools/make_gametdb_index.py switchtdb.zip romfs/data/gametdb.tsv

Output: one line per name key, sorted by byte value so the menu can binary
search the file without loading it: "<key>\t<gametdb id>\t<region>\n", where
region is U (USA), E (Europe), J (Japan) or A (all/other).
"""
import io
import sys
import xml.etree.ElementTree as ET
import zipfile

# U+00C0..U+017F folded to a base letter (" " for none). Same table as
# art/ArtNames.cpp.
FOLD = ("aaaaaa ceeeeiiii nooooo  uuuuy  aaaaaa ceeeeiiii nooooo  uuuuy yaaaaaaccccccccdd  eeeeeeeeeegggggggghh  "
        "iiiiiiiii iijjkk llllllll  nnnnnnn  oooooo  rrrrrrsssssssstttt  uuuuuuuuuuuuwwyyyzzzzzzs")
assert len(FOLD) == 0x180 - 0xC0

REGION_LETTER = {"ALL": "A", "USA": "U", "EUR": "E", "JPN": "J"}
REGION_RANK = {"A": 0, "U": 1, "E": 2, "J": 3}


def name_key(name):
    out = []
    for ch in name:
        cp = ord(ch)
        if cp < 128 and ch.isalnum():
            out.append(ch.lower())
        elif 0xC0 <= cp < 0x180:
            out.append(FOLD[cp - 0xC0])
        else:
            out.append(" ")
    return " ".join("".join(out).split())


def region_of(text):
    first = (text or "").split(",")[0].strip()
    return REGION_LETTER.get(first, "A")


def read_xml(path):
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            return z.read("switchtdb.xml")
    with open(path, "rb") as f:
        return f.read()


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: make_gametdb_index.py <switchtdb.zip|xml> <out.tsv>")
    data = read_xml(sys.argv[1])
    best = {}  # key -> (rank, id, region): one cover per name
    games = 0
    for _, game in ET.iterparse(io.BytesIO(data)):
        if game.tag != "game":
            continue
        games += 1
        gid = (game.findtext("id") or "").strip()
        if (game.findtext("type") or "Switch").strip() not in ("Switch", "eShop") or not gid:
            game.clear()
            continue
        region = region_of(game.findtext("region"))
        locales = game.findall("locale")
        titles = [l.findtext("title") for l in locales if l.get("lang") == "EN"]
        titles += [l.findtext("title") for l in locales if l.get("lang") != "EN"][:1]
        for title in filter(None, titles):
            key = name_key(title)
            if not key or "\t" in key:
                continue
            cand = (REGION_RANK[region], gid, region)
            if key not in best or cand < best[key]:
                best[key] = cand
        game.clear()
    keys = sorted(best, key=lambda k: k.encode("ascii"))
    with open(sys.argv[2], "wb") as f:
        for key in keys:
            _, gid, region = best[key]
            f.write(f"{key}\t{gid}\t{region}\n".encode("ascii"))
    print(f"{games} games, {len(keys)} names -> {sys.argv[2]}")


if __name__ == "__main__":
    main()
