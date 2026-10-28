PS5_HOST ?= ps5
PS5_PORT ?= 9021
ifdef PS5_PAYLOAD_SDK
  include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
  $(error PS5_PAYLOAD_SDK is undefined)
endif
CC ?= clang
ELF := retroverse-ps5-store.elf
CFLAGS := -Wall -Wextra -O2
CFLAGS += $(shell $(PS5_PAYLOAD_SDK)/bin/prospero-pkg-config libcurl --cflags)
LDADD := -lSceSsl -lSceHttp
LDADD += $(shell $(PS5_PAYLOAD_SDK)/bin/prospero-pkg-config libcurl --libs)
.PHONY: all clean test
all: $(ELF)
assets.inc: index.html embed.py
	python3 embed.py
catalog_data.inc: catalog_build.py
	python3 catalog_build.py
archive_data.inc: archive_catalog_build.py
	python3 archive_catalog_build.py
ca_data.inc: embed_ca.py
	python3 embed_ca.py
$(ELF): main.c curl_net.c curl_net.h assets.inc catalog_data.inc archive_data.inc ca_data.inc
	$(CC) $(CFLAGS) -o $@ main.c curl_net.c $(LDADD)
test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $(ELF)
clean:
	rm -f $(ELF) assets.inc catalog_data.inc archive_data.inc ca_data.inc
