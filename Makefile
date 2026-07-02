# VNLF App Store - Makefile

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra \
           -I/usr/include/gtk-3.0 \
           -I/usr/include/glib-2.0 \
           -I/usr/lib/x86_64-linux-gnu/glib-2.0/include \
           -I/usr/include/pango-1.0 \
           -I/usr/include/harfbuzz \
           -I/usr/include/cairo \
           -I/usr/include/gdk-pixbuf-2.0 \
           -I/usr/include/atk-1.0 \
		   -I/usr/include/vte-2.91 

# Try pkg-config first, fallback to manual flags
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 vte-2.91 2>/dev/null)
GTK_LIBS   := $(shell pkg-config --libs   gtk+-3.0 vte-2.91 2>/dev/null)

ifeq ($(GTK_CFLAGS),)
  GTK_LIBS := -lgtk-3 -lgdk-3 -lpangocairo-1.0 -lpango-1.0 \
              -lgdk_pixbuf-2.0 -lcairo-gobject -lcairo \
              -lgobject-2.0 -lglib-2.0 -latk-1.0 
endif

# fontconfig luôn cần để load font động
EXTRA_LIBS = -lfontconfig

TARGET   = store
SRCDIR   = src
BUILDDIR = build
SOURCES  = $(SRCDIR)/main.cpp
OBJECTS  = $(BUILDDIR)/main.o

# Đóng gói cài đặt của dev, sửa lại PREFIX để cài đặt vào thư mục mong muốn
PREFIX  = /usr/local
DATADIR = $(PREFIX)/bin
BINDIR  = $(PREFIX)/bin

.PHONY: all clean install uninstall run

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(GTK_LIBS) $(EXTRA_LIBS)
	@echo "✓ Build thành công: ./$(TARGET)"

$(BUILDDIR)/main.o: $(SRCDIR)/main.cpp
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) -c $< -o $@


run: all
	./$(TARGET)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

install: all
	install -d $(BINDIR)
	install -d $(DATADIR)/data/apps
	install -d $(DATADIR)/data/icons
	install -d $(DATADIR)/scripts
	install -d $(DATADIR)/fonts
	install -m 755 $(TARGET)           $(BINDIR)/$(TARGET)
	#install -m 644 data/apps.json      $(DATADIR)/data/
	install -m 644 data/apps/*.json    $(DATADIR)/data/apps/
	install -m 755 scripts/*.sh        $(DATADIR)/scripts/
	install -m 644 fonts/*.ttf         $(DATADIR)/fonts/ 2>/dev/null || true
	install -m 644 fonts/*.otf         $(DATADIR)/fonts/ 2>/dev/null || true
	@echo "✓ Đã cài đặt $(BINDIR)/$(TARGET)"
	@echo "✓ Dữ liệu + fonts: $(DATADIR)"

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	rm -rf $(DATADIR)
	@echo "✓ Đã gỡ cài đặt"
