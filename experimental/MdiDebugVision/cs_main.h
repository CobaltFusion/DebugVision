#pragma once

int cs_main(int argc, char* argv[]);

#ifdef _WIN32

#include <windows.h>
#include <string>
#include <vector>

// parse strings in dubble quotes as one argument
// dubble quoted strings can be empty
// arguments are separated by spaces
template <class C>
std::vector<std::string> ParseCommandline(const C* cmdline)
{
	std::vector<std::string> v;
	auto addString = [&v](std::string & s, bool checkForEmpty) {
		if (checkForEmpty && s.empty()) return;
		v.push_back(s);
		s.clear();
	};

	bool quoteOpen = false;
	std::string s;
	while (C c = *cmdline)
	{
		if (quoteOpen)
		{
			if (c == TEXT('\"'))
			{
				addString(s, false);
				quoteOpen = false;
			}
			else
			{
				s.push_back(static_cast<char>(c));
			}
		}
		else
		{
			if (c == TEXT('\"'))
				quoteOpen = true;
			else if (c == TEXT(' '))
				addString(s, true);
			else
				s.push_back(c);
		}
		cmdline++;
	}
	addString(s, quoteOpen);
	return v;
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	std::vector<std::string> arguments = ParseCommandline(GetCommandLineA());
	std::vector<char*> dataHolder(arguments.size(), nullptr);
	for (std::size_t i = 0; i < arguments.size(); i++)
		dataHolder[i] = &(arguments[i])[0];
	return cs_main(static_cast<int>(dataHolder.size()), dataHolder.data());
}

#else

int main(int argc, char* argv[])
{
	cs_main(argc, argv);
}

// #define main cs_main

#endif 
