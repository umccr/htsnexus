HERE:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
export PATH:=$(HERE)/node_modules/.bin:$(HERE)/runtime/node/bin:$(PATH)

all: runtime/node/bin/node
	runtime/node/bin/npm install

test:
	_mocha --compilers _js:streamline/register test/test._js

runtime/node/bin/node:
	mkdir -p runtime/node
	curl -L https://nodejs.org/dist/v5.5.0/node-v5.5.0-linux-x64.tar.xz | tar xJ -C runtime/node --strip-components 1

clean:
	rm -rf runtime/node node_modules

.PHONY: all test clean
