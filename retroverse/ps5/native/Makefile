ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK is undefined)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
HB := $(PS5_PAYLOAD_SDK)/target/user/homebrew
ELF := retroverse-ps5-tv-native.elf
CFLAGS := -O2 -Wall -Wextra -D_REENTRANT -I$(HB)/include -I$(HB)/include/SDL2 -I../full
CFLAGS += $(shell $(PS5_PAYLOAD_SDK)/bin/prospero-pkg-config libcurl --cflags)
CURL_LIBS := $(shell $(PS5_PAYLOAD_SDK)/bin/prospero-pkg-config --static --libs libcurl)
SDL_LIBS := $(HB)/lib/libSDL2main.a $(HB)/lib/libSDL2.a $(HB)/lib/libSDL2_image.a \
            $(HB)/lib/libpng.a $(HB)/lib/libjpeg.a $(HB)/lib/libwebp.a \
            $(HB)/lib/libsharpyuv.a
PS5_LIBS := -lSceSsl -lSceHttp -lScePad -lSceVideoOut -lSceAudioOut \
            -lSceSystemService -lSceUserService -lSceKeyboard -lSceImeDialog -lkernel_sys
LIBS := $(SDL_LIBS) $(CURL_LIBS) $(PS5_LIBS) -lm -lz -lbz2 -liconv

.PHONY: all clean
all: $(ELF)
../full/assets.inc ../full/catalog_data.inc ../full/archive_data.inc ../full/ca_data.inc:
	$(MAKE) -C ../full assets.inc catalog_data.inc archive_data.inc ca_data.inc

backend.o: ../full/main.c ../full/assets.inc ../full/catalog_data.inc ../full/archive_data.inc
	$(CC) $(CFLAGS) -Dmain=retroverse_web_server_main -c -o $@ ../full/main.c

curl_net.o: ../full/curl_net.c ../full/ca_data.inc ../full/curl_net.h
	$(CC) $(CFLAGS) -c -o $@ ../full/curl_net.c

ui.o: main.c ../full/curl_net.h
	$(CC) $(CFLAGS) -c -o $@ main.c

$(ELF): backend.o curl_net.o ui.o
	$(CC) $(CFLAGS) -o $@ ui.o backend.o curl_net.o $(LIBS)

clean:
	rm -f $(ELF) backend.o curl_net.o ui.o
