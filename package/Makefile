
install:
	if [ ! -d "/usr/local/etc/vpu" ]; then mkdir "/usr/local/etc/vpu"; fi
	cp -f etc/* /usr/local/etc/vpu/
	cp -f vpu.yaml /usr/local/etc/software/vpu.yaml
	cp -f bin/* /usr/local/bin/

update:
	cp -f bin/* /usr/local/bin/
	cp -f vpu.yaml /usr/local/etc/software/vpu.yaml

uninstall:
	rm -f /usr/local/bin/vpu
	rm -rf /usr/local/etc/vpu
	rm -f /usr/local/etc/software/vpu.yaml

