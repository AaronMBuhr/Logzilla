#pragma once

class  JsonLogMessageHandler {
public:
	virtual void handleJsonMessage(const char* json_cstr) = 0;
};