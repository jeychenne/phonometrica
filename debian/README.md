# Building a .deb package for Phonometrica

## File placement

Copy the provided files into your source tree:

```
phonometrica/
├── debian/
│   ├── changelog
│   ├── control
│   ├── copyright
│   ├── rules
│   └── source/
│       └── format
├── dist/
│   └── linux/
│       ├── phonometrica.desktop
│       └── org.phonometrica.Phonometrica.metainfo.xml
```


## Install build dependencies (on Debian 13 / trixie)

```bash
sudo apt install \
  build-essential debhelper cmake \
  qt6-base-dev qt6-svg-dev libqt6printsupport6 \
  libqscintilla2-qt6-dev libsndfile1-dev libasound2-dev \
  python3-sphinx devscripts fakeroot lintian
```

On Ubuntu 24.04+, the same package names should work. If
`libqscintilla2-qt6-dev` is missing, try `libqscintilla2-qt6-l10n`
or build QScintilla from source.


## Build the .deb

From the root of your source tree:

```bash
# Option A: Full source+binary build (recommended for redistribution)
dpkg-buildpackage -us -uc -b

# Option B: Quick unsigned binary-only build
dpkg-buildpackage -us -uc -b --no-sign
```

Both commands produce a `.deb` file one directory up:

```
../phonometrica_0.9.0-1_amd64.deb
```

## Verify the package

```bash
# Lint for policy violations
lintian ../phonometrica_0.9.0-1_amd64.deb

# Inspect contents
dpkg-deb -c ../phonometrica_0.9.0-1_amd64.deb

# Test install
sudo dpkg -i ../phonometrica_0.9.0-1_amd64.deb
# If dependencies are missing:
sudo apt -f install
```

## What gets installed

- `/usr/local/bin/phonometrica` — the binary
- `/usr/local/share/applications/phonometrica.desktop` — desktop integration
- `/usr/local/share/metainfo/org.phonometrica.Phonometrica.metainfo.xml` — AppStream metadata
- `/usr/local/share/icons/hicolor/scalable/apps/org.phonometrica.Phonometrica.svg` — app icon


## Cross-distro notes

The resulting .deb works on any distro that has the same or newer
versions of the linked shared libraries (Qt6, libsndfile, ALSA,
QScintilla). In practice:

- **Debian 13 (trixie)** — native target, will work directly
- **Ubuntu 24.04+** — should work if Qt6 versions are compatible
- **Ubuntu 24.10 / 25.04** — best match for Debian 13 library versions

If you want maximum portability, you can also consider building
inside a minimal Debian 13 Docker container or chroot (pbuilder/sbuild)
so the binary links against the oldest supported library versions.


## Version bumps

When you release a new version, update:

1. `CMakeLists.txt` — `PHON_VERSION_*` variables
2. `debian/changelog` — add a new entry at the top:

```bash
dch -v 1.0.0-1 "Release 1.0.0"
```

3. `dist/linux/org.phonometrica.Phonometrica.metainfo.xml` — add a
   new `<release>` element

