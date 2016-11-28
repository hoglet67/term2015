#!/bin/bash
ca65 -l TERM.log -o TERM.o TERM.asm 
ld65 TERM.o -C atom.cfg -o TERM
md5sum TERM
ca65 -l VTERM.log -o VTERM.o VTERM.asm 
ld65 VTERM.o -C atom.cfg -o VTERM
md5sum VTERM
