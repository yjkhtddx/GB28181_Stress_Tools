#pragma once

typedef enum {
	STATUS_TYPE,
	PULL_STREAM_PORT_TYPE,
	PULL_STREAM_PROTOCOL_TYPE,
	RES_TIME
} MESSAGE_TYPE;

struct Message {
	MESSAGE_TYPE type;
	const char * content;
};
