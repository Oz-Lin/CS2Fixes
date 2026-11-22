#include "utils/countdown_bridge.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace ozlin::countdown_bridge;

int main()
{
	char command[kCommandBytes];
	assert(Format(command, 15, "Hold for 15 seconds!"));
	assert(std::string_view(command) == "css_countdown_text 15 \"Hold for 15 seconds!\"");
	assert(Format(command, 65, "守住门口 65 秒"));
	assert(std::string_view(command) == "css_countdown_text 65 \"守住门口 65 秒\"");
	assert(Format(command, 14, "Hold [100%], please!"));
	assert(std::string_view(command) == "css_countdown_text 14 \"Hold [100%], please!\"");
	assert(Format(command, 5, "A"));
	assert(Format(command, 86400, std::string(kMaxTextBytes, 'a')));
	for (const auto bad : {std::string_view{}, std::string_view("    "), std::string_view("\""),
						   std::string_view("\\"), std::string_view(";"), std::string_view("\n"),
						   std::string_view("\r"), std::string_view("\t"), std::string_view("\x7f"),
						   std::string_view("A\0B", 3)})
	{
		assert(!Format(command, 9, bad));
		assert(command[0] == '\0');
	}
	assert(!Format(command, 4, "A"));
	assert(!Format(command, 86401, "A"));
	assert(!Format(command, 9, std::string(kMaxTextBytes + 1, 'a')));
	std::cout << "CountDown bridge tests passed\n";
}
