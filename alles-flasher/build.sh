#!/usr/bin/env bash
# rm -fr build dist
VERSION=5.0.0
NAME=Alles-Flasher

pyinstaller --log-level=DEBUG \
            --noconfirm \
            --windowed \
            build-on-mac.spec

# https://github.com/sindresorhus/create-dmg
create-dmg $NAME-$VERSION.dmg dist/$NAME-$VERSION.app
mv "$NAME-$VERSION.dmg" dist/$NAME-$VERSION.dmg
