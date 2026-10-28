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
LDADD := -lSceSsl -lSceHttp
.PHONY: all clean test
all: $(ELF)
assets.inc: index.html embed.py
	python3 embed.py
catalog_data.inc: catalog_build.py
	python3 catalog_build.py
$(ELF): main.c assets.inc catalog_data.inc
	$(CC) $(CFLAGS) -o $@ main.c $(LDADD)
test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $(ELF)
clean:
	rm -f $(ELF) assets.inc catalog_data.inc
