# Brainfuck++ Compiler

**Lightweight compiler of a superset of brainfuck made by me, written in C++**

## What is BF++?

Brainfuck++ is a superset of brainfuck as mentioned previously, but this adds more important stuff like compatibility with libc, different sizes, and function calling.
This outputs valid x86-64 Assembly with GAS syntax, this only works on linux.

## Why?

No real reason, this was just something I did for fun, I felt like the language lacked actual usage
so I added some, this is nowhere near perfect or for real use, but it is pretty unique.

## New features

- Supports both object (.o) and assembly (.s/asm) outputs.
- Supports non-varargs and non-float C functions.
- Libc compatible, as mentioned before.
- Uses SystemV ABI and Linux Syscalls

## How to compile and run

There is a makefile with the project, just run "make" and it should compile it.

## Usage

```bash
./bfpp input.bf -o out.s
# OR
./bfpp input.bf -o out.o # calls your assembler
# THEN
gcc out.o -o out # or clang
./out
```

## Contribution

This project is as-is. Anyone can fork this project and change it however they want.