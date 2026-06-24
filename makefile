C_SOURCES = $(wildcard src/c/*.c) $(wildcard src/c/*.h)
JS_SOURCES = $(wildcard src/pkjs/*.js)

build/magnetism.pbw: $(C_SOURCES) $(JS_SOURCES) package.json
	pebble build

install: build/magnetism.pbw
	pebble install 

tags: $(C_SOURCES) build/include/message_keys.auto.h
	ctags -R src/c build/include ~/.pebble-sdk/SDKs/current/sdk-core/pebble

