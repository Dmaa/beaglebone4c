#!/bin/bash
default: lab4c_tls lab4c_tcp

lab4c_tcp: lab4c_tcp.c
	gcc -g -pthread -lm -lmraa lab4c_tcp.c -o lab4c_tcp
lab4c_tls:
	gcc -g -pthread -lm -lmraa lab4c_tls.c -o lab4c_tls
clean:
	rm -f lab4c_tcp lab4c_tls  *.tar.gz
dist:
	tar -czvf lab4c-304815342.tar.gz lab4c_tcp.c lab4c_tls.c Makefile README
