#!/bin/bash
ca65 -l TERM.log -o TERM.o TERM.asm 
ld65 TERM.o -C atom.cfg -o TERM
md5sum TERM
