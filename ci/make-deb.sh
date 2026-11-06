#!/bin/sh
# Builds a Debian package from a Linux install tree (cmake --build build -t install).
# The programs and the CopperSpice libraries they come with go to /opt/debugvision, with links in /usr/bin and a
# desktop entry. The dependencies on system libraries are computed with dpkg-shlibdeps, so a package built on
# Debian 12 depends on the Debian 12 library packages and one built on Debian 13 on the Debian 13 ones.
# usage: ci/make-deb.sh <install dir> <version, e.g. 0.1.0> [output dir]
set -eu

install=$(cd "$1" && pwd)
version=$2
out=$(mkdir -p "${3:-.}" && cd "${3:-.}" && pwd)
src=$(cd "$(dirname "$0")/.." && pwd)

. /etc/os-release
arch=$(dpkg --print-architecture)
debversion="$version-1~deb${VERSION_ID}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
root=$work/debugvision

mkdir -p "$root/DEBIAN" "$root/opt/debugvision" "$root/usr/bin" "$root/usr/share/applications" \
    "$root/usr/share/icons/hicolor/256x256/apps" "$root/usr/share/doc/debugvision"
cp -a "$install/." "$root/opt/debugvision/"
for program in debugvision dvcli dvtest; do
    if [ ! -x "$root/opt/debugvision/$program" ]; then
        echo "error: $install/$program is missing" >&2
        exit 1
    fi
    ln -s "/opt/debugvision/$program" "$root/usr/bin/$program"
done
find "$root/opt/debugvision" -type f \( -name '*.so' -o -perm -u+x \) -exec strip --strip-unneeded {} +

cp "$src/src/application/icons/debugvision.png" "$root/usr/share/icons/hicolor/256x256/apps/debugvision.png"
cat > "$root/usr/share/applications/debugvision.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=DebugVision
GenericName=Log Viewer
Comment=See what your software does: view, filter and visualize log output
Exec=debugvision %F
Icon=debugvision
Terminal=false
Categories=Development;Debugger;
EOF
{
    echo "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/"
    echo "Upstream-Name: DebugVision"
    echo "Source: https://github.com/CobaltFusion/DebugVision"
    echo
    echo "Files: *"
    echo "License: see below"
    echo
    cat "$src/LICENSE"
} > "$root/usr/share/doc/debugvision/copyright"

# the system libraries needed by the programs, the bundled CopperSpice libraries and its platform plugin
mkdir -p "$work/debian"
printf 'Source: debugvision\n\nPackage: debugvision\nArchitecture: any\n' > "$work/debian/control"
binaries=$(find "$root/opt/debugvision" -type f \( -name '*.so*' -o -perm -u+x \) -exec sh -c 'head -c 4 "$1" | grep -q ELF' _ {} \; -print)
depends=$(cd "$work" && dpkg-shlibdeps -O --ignore-missing-info -l"$root/opt/debugvision" $binaries 2>/dev/null | sed -n 's/^shlibs:Depends=//p')
if [ -z "$depends" ]; then
    echo "error: dpkg-shlibdeps found no dependencies" >&2
    exit 1
fi

find "$root" -type d -exec chmod 755 {} +
find "$root" -type f -exec chmod go-w {} +
size=$(du -sk "$root" | cut -f1)
cat > "$root/DEBIAN/control" <<EOF
Package: debugvision
Version: $debversion
Architecture: $arch
Maintainer: Jan Wilmans <janwilmans@gmail.com>
Installed-Size: $size
Depends: $depends
Section: devel
Priority: optional
Homepage: https://github.com/CobaltFusion/DebugVision
Description: log viewer that shows what your software does
 DebugVision collects log lines from files, program output, the network and
 web servers, filters and highlights them, and turns them into spans and
 events on a timeline. Includes dvcli, the command line version, and dvtest,
 which generates test data for the timeline.
EOF

deb="$out/debugvision_${debversion}_${arch}.deb"
dpkg-deb --root-owner-group --build "$root" "$deb"
echo "Depends: $depends"
echo "$deb"
