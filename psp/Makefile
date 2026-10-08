TARGET = flxtr
VENDOR = ../third_party/tilefinch-media
VPATH = $(VENDOR)/src
OBJS = main.o network.o http.o player.o network_update.o budget.o media_backend.o media_backend_psp.o media_mp4.o media_source.o media_h264_psp_compat.o systemctrl_user_imports.o media_backend_psp_imports.o
INCDIR = $(VENDOR)/include $(VENDOR)/src
CFLAGS = -Os -std=c11 -Wall -Wextra -ffunction-sections -fdata-sections -DFLXTR_VERSION=\"$(VERSION)\" -DFLXTR_BUILD=$(BUILD)
ASFLAGS = $(CFLAGS)
LDFLAGS =
LIBS = -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lcjson -latomic -lpspdebug -lm -lcglue -lc -lpspsdk -Wl,--whole-archive -lpspuser -Wl,--no-whole-archive -lpspmpeg -lpspmpegbase -lpspaudiocodec -lpspaudio -lpspgu -lpspge -lpspdisplay -lpspctrl -lpspdebug -lpsppower -lpsprtc -lpsputility -lpspnet -lpspnet_inet -lpspnet_apctl -lpspnet_resolver -latomic -lm
BUILD_PRX = 1
BUILD ?= 0
VERSION ?= development
PSP_FW_VERSION = 660
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = FlXtR PSP streaming beta
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
