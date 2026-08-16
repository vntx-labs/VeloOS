.PHONY: all run clean

all: os.iso

os.iso: os.bin matrix.bin
	mkdir -p iso_root
	cp matrix.bin iso_root/
	genisoimage -R -o os.iso iso_root

os.bin: stage1.bin stage2.bin matrix.bin
	cat stage1.bin stage2.bin > os.bin
	python3 mkfs.py

stage1.bin: stage1.asm
	nasm -f bin stage1.asm -o stage1.bin

# Matrix-Anwendung mit -O2 für sauberes Inlining
matrix.o: matrix.c
	gcc -m64 -O2 -ffreestanding -fno-pie -fno-asynchronous-unwind-tables -fno-toplevel-reorder -mcmodel=kernel -nostdlib -c matrix.c -o matrix.o

matrix.bin: matrix.o matrix.ld
	ld -m elf_x86_64 -T matrix.ld --oformat binary matrix.o -o matrix.bin

# C-Kernel Kompiliervorgang
kernel.o: kernel.c
	gcc -m64 -ffreestanding -fno-pie -fno-toplevel-reorder -mcmodel=kernel -nostdlib -c kernel.c -o kernel.o

# NEU: Das Tastatur-Subsystem kompilieren
keyboard.o: keyboard.c keyboard.h
	gcc -m64 -ffreestanding -fno-pie -fno-toplevel-reorder -mcmodel=kernel -nostdlib -c keyboard.c -o keyboard.o

# ERWEITERT: keyboard.o als Abhängigkeit hinzugefügt und fest in das Kernel-Binary gelinkt
stage2.bin: stage2.asm kernel.o keyboard.o
	nasm -f elf64 stage2.asm -o stage2.o
	ld -m elf_x86_64 -T linker.ld --oformat binary stage2.o kernel.o keyboard.o -o stage2.bin

run: os.iso os.bin
	qemu-system-x86_64 -drive format=raw,file=os.bin -cdrom os.iso -d int,cpu_reset
clean:
	rm -f *.bin *.o *.iso
	rm -rf iso_root
