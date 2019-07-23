export PATH := $(shell pwd)/../../../host/bin/:$(PATH)


all: build_imx8_gcc/sof-imx8.ri

build_imx8_gcc/sof-imx8.ri:
	./scripts/xtensa-build-all.sh imx8
	cp build_imx8_gcc/sof-imx8.ri ../../../target/lib/firmware/imx/sof/sof-imx8.ri

compile:
	make -C build_imx8_gcc
	cp build_imx8_gcc/sof-imx8.ri ../../../target/lib/firmware/imx/sof/sof-imx8.ri
.PHONY : all build_imx8_gcc/sof-imx8.ri compile
