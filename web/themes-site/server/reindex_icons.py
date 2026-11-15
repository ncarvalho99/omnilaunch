#!/usr/bin/env python3
"""Build the icon store: one zip per icon pack and icons.json.

Layout under ICON_ROOT (default /srv/themes/icons):

    <pack-id>/
        pack.json          {"id","name","author","version","description"}
        src/*.png          the icons, named like romfs/icons/<file>.png
        store_preview.png  1280x720 preview (optional; built from src when absent)

Produced next to them:

    <pack-id>/preview.jpg               store preview, 1280x720 JPEG
    <pack-id>/icons-<sha12>.zip         pack.json + PNGs at the archive root
    <ICON_ROOT>/../icons.json           the catalogue the console reads

The zip name carries the content hash, so a changed pack gets a new URL and no
cache serves the old one. Old zips of a pack are removed after the new one is
written. Only PNG files whose names are in ALLOWED are packed: a pack can only
replace icons the menu actually loads.
"""
import hashlib
import io
import json
import os
import re
import sys
import zipfile

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # previews then need to be supplied by hand
    Image = None

ICON_ROOT = os.environ.get('ICON_ROOT', '/srv/themes/icons')
CATALOG = os.path.join(os.path.dirname(ICON_ROOT.rstrip('/')), 'icons.json')
ID_RE = re.compile(r'^[a-z0-9_-]{2,64}$')

# Every file the menu loads from romfs:/icons (see core/IconPack.hpp).
ALLOWED = {
    'album.png', 'battery.png', 'battery_bolt.png', 'bell.png', 'browser.png',
    'controller.png', 'discord.png', 'eshop.png', 'friends.png', 'gamecard.png',
    'hekate.png', 'homebrew.png', 'manager.png', 'media_center.png',
    'mii_editor.png', 'music_player.png', 'network.png', 'news.png',
    'nextendo.png', 'page_arrow_left.png', 'page_arrow_right.png',
    'pointer_hand.png', 'power.png', 'settings.png', 'sphaira.png',
    'storage.png', 'themes.png', 'toggle.png', 'warawara_u.png',
    'widget_battery_controller.png', 'widget_battery_joycon_left.png',
    'widget_battery_joycon_right.png', 'widget_battery_switch.png',
}
# Shown in a generated preview, in this order.
PREVIEW_ORDER = [
    'album.png', 'controller.png', 'gamecard.png', 'homebrew.png', 'music_player.png',
    'mii_editor.png', 'network.png', 'power.png', 'settings.png', 'toggle.png',
    'themes.png', 'eshop.png', 'friends.png', 'hekate.png', 'manager.png',
    'media_center.png', 'news.png', 'sphaira.png', 'storage.png', 'nextendo.png',
    'discord.png', 'browser.png',
]
# Packs listed first, in this order; the rest by name.
FEATURED = ['switchu-anime', 'switchu-android', 'switchu-cartoon']


def make_preview(pack_dir, name, out_path):
    """A 1280x720 sheet of the pack's icons, like the hand-made previews."""
    if Image is None:
        return False
    src = os.path.join(pack_dir, 'src')
    files = [f for f in PREVIEW_ORDER if os.path.isfile(os.path.join(src, f))][:22]
    if not files:
        return False
    W, H = 1280, 720
    img = Image.new('RGB', (W, H), (236, 235, 244))
    draw = ImageDraw.Draw(img)
    font = None
    for cand in ('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
                 'C:/Windows/Fonts/segoeuib.ttf'):
        if os.path.isfile(cand):
            font = ImageFont.truetype(cand, 56)
            break
    if font is None:
        font = ImageFont.load_default()
    tw = draw.textlength(name, font=font)
    draw.text(((W - tw) / 2, 44), name, fill=(32, 30, 58), font=font)
    per_row = 11
    rows = [files[i:i + per_row] for i in range(0, len(files), per_row)]
    size, gap = 88, 16
    y = 220 if len(rows) > 1 else 300
    for row in rows:
        total = len(row) * size + (len(row) - 1) * gap
        x = (W - total) // 2
        for f in row:
            icon = Image.open(os.path.join(src, f)).convert('RGBA')
            icon.thumbnail((size, size), Image.LANCZOS)
            ox = x + (size - icon.width) // 2
            oy = y + (size - icon.height) // 2
            img.paste(icon, (ox, oy), icon)
            x += size + gap
        y += size + 92
    img.save(out_path, 'JPEG', quality=88, optimize=True)
    return True


def build_pack(pack_id):
    pack_dir = os.path.join(ICON_ROOT, pack_id)
    meta_path = os.path.join(pack_dir, 'pack.json')
    src = os.path.join(pack_dir, 'src')
    if not os.path.isfile(meta_path) or not os.path.isdir(src):
        print('skip %s: needs pack.json and src/' % pack_id)
        return None
    with open(meta_path, encoding='utf-8') as fh:
        meta = json.load(fh)
    meta['id'] = pack_id
    name = str(meta.get('name') or pack_id)
    icons = sorted(f for f in os.listdir(src) if f in ALLOWED)
    ignored = sorted(f for f in os.listdir(src) if f not in ALLOWED)
    if ignored:
        print('%s: ignoring %s' % (pack_id, ', '.join(ignored)))
    if not icons:
        print('skip %s: no known icon files' % pack_id)
        return None

    # Preview: the pack's own store_preview.png, else one generated from src.
    preview = os.path.join(pack_dir, 'preview.jpg')
    hand = os.path.join(pack_dir, 'store_preview.png')
    if os.path.isfile(hand) and Image is not None:
        Image.open(hand).convert('RGB').resize((1280, 720), Image.LANCZOS).save(
            preview, 'JPEG', quality=88, optimize=True)
    elif not os.path.isfile(preview):
        make_preview(pack_dir, name, preview)

    manifest = {
        'id': pack_id,
        'name': name,
        'author': str(meta.get('author') or 'OmniLaunch'),
        'version': str(meta.get('version') or '1.0.0'),
    }
    # Deterministic zip: fixed timestamps, sorted entries.
    buf = io.BytesIO()
    installed = 0
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as zf:
        info = zipfile.ZipInfo('pack.json', date_time=(2020, 1, 1, 0, 0, 0))
        data = (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
        zf.writestr(info, data, zipfile.ZIP_DEFLATED)
        installed += len(data)
        for f in icons:
            with open(os.path.join(src, f), 'rb') as fh:
                data = fh.read()
            info = zipfile.ZipInfo(f, date_time=(2020, 1, 1, 0, 0, 0))
            # PNGs are already compressed: store them.
            zf.writestr(info, data, zipfile.ZIP_STORED)
            installed += len(data)
    blob = buf.getvalue()
    digest = hashlib.sha256(blob).hexdigest()
    zip_name = 'icons-%s.zip' % digest[:12]
    zip_path = os.path.join(pack_dir, zip_name)
    if not os.path.isfile(zip_path):
        tmp = zip_path + '.tmp'
        with open(tmp, 'wb') as fh:
            fh.write(blob)
        os.replace(tmp, zip_path)
    for old in os.listdir(pack_dir):
        if old.startswith('icons-') and old.endswith('.zip') and old != zip_name:
            os.remove(os.path.join(pack_dir, old))

    entry = {
        'id': pack_id,
        'name': name,
        'author': manifest['author'],
        'version': manifest['version'],
        'files': len(icons),
        'package': 'icons/%s/%s' % (pack_id, zip_name),
        'packageBytes': len(blob),
        'packageSha256': digest,
        'installedBytes': installed,
    }
    if meta.get('description'):
        entry['description'] = str(meta['description'])
    if os.path.isfile(preview):
        entry['preview'] = 'icons/%s/preview.jpg?v=%s' % (pack_id, digest[:8])
    return entry


def main():
    if not os.path.isdir(ICON_ROOT):
        print('no icon root at %s' % ICON_ROOT)
        return 0
    packs = []
    for pack_id in sorted(os.listdir(ICON_ROOT)):
        if not ID_RE.match(pack_id) or not os.path.isdir(os.path.join(ICON_ROOT, pack_id)):
            continue
        try:
            entry = build_pack(pack_id)
        except Exception as ex:  # one broken pack must not empty the store
            print('pack %s failed: %s' % (pack_id, ex))
            entry = None
        if entry:
            packs.append(entry)
    rank = {pid: i for i, pid in enumerate(FEATURED)}
    packs.sort(key=lambda e: (rank.get(e['id'], len(rank)), e['name'].lower()))
    builtin = {
        'id': 'default', 'name': 'OmniLaunch', 'author': 'OmniLaunch',
        'version': '', 'builtin': True,
    }
    # Preview of the built-in set: default-preview.jpg in ICON_ROOT (not a pack:
    # the name fails ID_RE).
    if os.path.isfile(os.path.join(ICON_ROOT, 'default-preview.jpg')):
        builtin['preview'] = 'icons/default-preview.jpg'
    out = {'schemaVersion': 1, 'packs': [builtin] + packs}
    tmp = CATALOG + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as fh:
        json.dump(out, fh, ensure_ascii=False, indent=2)
        fh.write('\n')
    os.replace(tmp, CATALOG)
    print('indexed %d icon pack(s) into %s' % (len(packs), CATALOG))
    return 0


if __name__ == '__main__':
    sys.exit(main())
