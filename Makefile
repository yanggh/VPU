
VER = $(shell cat package/vpu-info.yaml | grep " version:" | cut -b 12-15)
BUILD    = $(shell git log | grep -cE 'Author:')
BUILDSHA = $(shell git rev-parse --short HEAD)

BSTR = $(shell printf %03d $(BUILD))

ALL: package

clean:
	make -C src clean
	rm -f package/*.tar.gz

build:
	make -C src

package: src/vpu
	if [ ! -d "package/bin" ]; then mkdir -p "package/bin"; fi
	if [ ! -d "package/lib" ]; then mkdir -p "package/lib"; fi
	if [ ! -d "package/etc" ]; then mkdir -p "package/etc"; fi
	cp src/vpu package/bin/
	cp etc/* package/etc/
	awk '($$2== "BUILDSTR") gsub("BUILDSTR","$(BSTR)")' package/vpu-info.yaml > package/vpu.yaml
	cd package && tar cpfz vpu-$(VER).$(BSTR).tar.gz bin etc lib Makefile vpu.yaml


