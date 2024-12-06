#
# Makefile for non-Microsoft compilers
#

all: MakeAll

MakeAll:
	mkdir Release
	$(MAKE) -C node2vec

clean:
	$(MAKE) clean -C node2vec
	rm -rf Debug Release ipch *.tgz *.zip

bin-linux:
	mv Release Snap-$(VERSION)-Ubuntu18.04
	tar zcvf Snap-$(VERSION)-Ubuntu18.04.tgz Snap-$(VERSION)-Ubuntu18.04
	mv Snap-$(VERSION)-Ubuntu18.04 Release

bin-mac:
	mv Release Snap-$(VERSION)-macOS14.10
	tar zcvf Snap-$(VERSION)-macOS14.10.tgz Snap-$(VERSION)-macOS14.10
	mv Snap-$(VERSION)-macOS14.10 Release

bin-win:
	mv Release Snap-$(VERSION)-Win32
	zip -r Snap-$(VERSION)-Win32.zip Snap-$(VERSION)-Win32/*.exe
	mv Snap-$(VERSION)-Win32 Release

