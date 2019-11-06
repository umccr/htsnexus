HERE:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
export PATH:=$(HERE)/node_modules/.bin:$(PATH)

all:
	npm ci 

test:
	_mocha --compilers _js:streamline/register test/test._js

clean:
	rm -rf runtime/node node_modules

.PHONY: all test clean
