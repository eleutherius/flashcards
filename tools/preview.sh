#!/bin/sh
# Рендерить основні екрани в build/shots/*.png через хостовий мок InkView.
set -e
cd "$(dirname "$0")/.."
rm -rf build/root build/shots
mkdir -p build/root/flashcards build/shots

S="INIT;SHOW;SHOT build/shots/1-decks.png;TAP 700 350;SHOT build/shots/2-front.png;TAP 700 900;SHOT build/shots/3-back.png"
MOCK_SCRIPT="$S" ./build/preview > /dev/null

S="INIT;SHOW;TAP 700 350"
for i in $(seq 1 20); do S="$S;KEY 25;KEY 25"; done
MOCK_SCRIPT="$S;SHOT build/shots/4-done.png" ./build/preview > /dev/null

ls build/shots
