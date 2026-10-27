# Збірка для PocketBook на платформі B288 (InkPad Color 741, InkPad 3, Touch HD 3 тощо).
#   make            — крос-компіляція build/Flashcards.app
#   make test       — юніт-тести логіки на хості
#   make preview    — PNG-знімки екранів через хостовий мок InkView
#   make package    — zip для копіювання на книжку
#
# SDK: git clone --branch 6.5 https://github.com/pocketbook/SDK_6.3.0  (тека SDK-B288)

SDK      ?= $(CURDIR)/sdk/SDK-B288
CROSS    := $(SDK)/usr/bin/arm-obreey-linux-gnueabi-
CC       := $(CROSS)gcc
STRIP    := $(CROSS)strip
# тулчейну потрібні власні libmpfr/libgmp з SDK — лише для його команд, не для cp/zip
XENV     := LD_LIBRARY_PATH=$(SDK)/usr/lib

CFLAGS   := -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter -fsigned-char \
            -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=softfp
LDLIBS   := -linkview -lm

APP      := build/Flashcards.app
SRC      := src/main.c src/deck.c
HDR      := src/deck.h src/sample_deck.h

HOST_INC := $(SDK)/usr/arm-obreey-linux-gnueabi/sysroot/usr/local/include

.PHONY: all test preview package clean

all: $(APP)

src/sample_deck.h: decks/english-basics.tsv tools/embed.py
	python3 tools/embed.py $< $@ sample_deck_tsv

$(APP): $(SRC) $(HDR)
	@mkdir -p build
	$(XENV) $(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)
	$(XENV) $(STRIP) $@

test:
	@mkdir -p build
	cc -std=gnu99 -Wall -Wextra -fsanitize=address,undefined -g -Isrc test/test_deck.c src/deck.c -o build/test_deck
	./build/test_deck build

build/preview: $(SRC) $(HDR) mock/mock_inkview.c
	@mkdir -p build/mockinc build/root/flashcards
	cp $(HOST_INC)/inkview.h $(HOST_INC)/hwconfig.h $(HOST_INC)/inkplatform.h build/mockinc/
	cc -std=gnu99 -O1 -g -Wall -Wno-unused-parameter -Isrc -Ibuild/mockinc $$(pkg-config --cflags freetype2 libpng) \
	   -DFC_ROOT='"$(CURDIR)/build/root/flashcards"' -DFC_ROOT_SD='"$(CURDIR)/build/root/sd"' \
	   -o $@ $(SRC) mock/mock_inkview.c $$(pkg-config --libs freetype2 libpng) -lm

preview: build/preview
	./tools/preview.sh

package: $(APP)
	rm -rf build/pkg build/Flashcards-PocketBook.zip
	mkdir -p build/pkg/applications build/pkg/flashcards
	cp $(APP) build/pkg/applications/
	cp decks/*.tsv build/pkg/flashcards/
	cp README.md build/pkg/
	cd build/pkg && zip -qr ../Flashcards-PocketBook.zip .

clean:
	rm -rf build src/sample_deck.h
