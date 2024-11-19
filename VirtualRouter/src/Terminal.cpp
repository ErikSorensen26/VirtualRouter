#include <Terminal.h>

#ifdef _WIN32
#include <Windows.h>
#elif __linux__
#include <X11/Xlib.h>
#include <X11/extensions/xtestconst.h>
#endif

using namespace std;


Terminal::Terminal(bool isDebug) : Console() {

	std::cout << "terminal" << std::endl;

	debug = isDebug;

	er.name = "<error>";
	cr.name = "<cr>";

	json.clear();
	std::string filename = "../VirtualRouter/Configs/Commands.json";
	std::ifstream file(filename);
	if (file.is_open()) {
	    file >> json;
	    file.close();
	} else {
	    std::cerr << "Failed to open file: " << filename << std::endl;
	}
	switchMode(mode.globalConfiguration);

	initConsole();
	initConfigs();

	recover();
}

void Terminal::recover() {
	vector<string> running = recoverXml();
	for (string& str : running) {
		Process(str);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	//switchMode(mode.userExec);
}

void Terminal::Input() {
	std::string hostname = Global::getInstance().Hostname();
	cursorPos = 0;
	cout << Global::getInstance().Hostname() << currentMode;
#ifdef _WIN32
	initialLineLength = hostname.size() + currentMode.size();
#else 
	initialLineLength = hostname.size() + currentMode.size() + 1;
#endif
	string Command = input();
	if (isValidIPv6(Command)) {
		cout << convertToFullIPv6(Command);
	}
	if (Command == "CRT-Z" && currentMode != mode.userExec) {
		switchMode(mode.privilegedExec);
	}
	if (Command != "VK_UP" && Command != "VK_DOWN") {
		Process(Command);
	}
	cout << endl;
}

string Terminal::FixCommand(const string& command) {
	if (command == "") {
		return "";
	}
	vector<string> normalStream = extractWords(command);
	string normalize;
	for (char c : command) {
		if (isspace(c) || c == '\t') {
			normalize += c;
		}
		else {
			normalize += tolower(c);
		}
	}
	currentDir = jsonDir;

	nextWordHelp = false;

	vector<string> stream = extractWords(normalize);
	vector<com> previousList;
	string oldstring;
	string previous;
	string newstring;
	string volitileString;

	int i = 0;
	bool m = true;
	run = true;
	no = false;
	successMatch = false;
	help = false;
	endcommand = false;
	line = false;
	for (string name : stream) {
		if (name == "?" || name == "vk_tab") {
			help = true;
		}
	}
	if (stream.size() > 0) {
		if (stream[0] == "do" && stream[1] != "exit" && stream[1] != "conf" && stream[1] != "configure" && currentMode != mode.userExec && currentMode != mode.privilegedExec) {
			globalCommand = true;
			string prevMode = currentMode;
			nlohmann::json prevJson = jsonDir;
			pugi::xml_node prevXML = config_node;
			switchMode(mode.privilegedExec);
			string nextCommand = command.substr(2);
			Process(nextCommand);
			currentDir.clear();
			switchMode(prevMode);
			config_node = prevXML;
			jsonDir = prevJson;
			return "error";
		} else if (stream[0] == "no" && !help) {
			string newCommand = FixCommand(command.substr(3));
			no = true;
			return newCommand;
		}
	}
	if (!stream.empty()) {
		if (stream[0] == "?" || stream[0] == "vk_tab") {
			successMatch = true;
		}
	}
	for (string name : stream) {
		if (line) {
			newstring = newstring + " " + normalStream[i];
			volitileString = volitileString + " " + normalStream[i];
		} else {
			matchPattern = false;
			matchPatternEnd = false;
			if (run) {
				vector<com> commandlist = GetCommandList(currentDir, name, m);
			if ((name == "?") && !successMatch && !previousList.empty() && !nextWordHelp && !endcommand) {
					newstring += name;
					volitileString += name;
					nextLine = oldstring + " ";
					if (previousList[0].name != "<cr>") {
						printNames(previousList);
					} else {
						nextLine = format(command);
					}
				} else if (name == "vk_tab" && !nextWordHelp) {
					if (!previousList.empty() && previousList.size() != 1) {
						newstring += name;
						volitileString += name;
						nextLine = oldstring + " ";
					} else if (previousList.empty()) {
						nextLine = format(command);
					} else {
						nextLine = oldstring + " ";
						nextLine = nextLine.substr(0, nextLine.size() - 1);
						int pos;
						bool isSpace = false;
						for (int c = 0; c <= nextLine.size(); c++) {
							if (nextLine[c] == ' ') {
								pos = c;
								isSpace = true;
							}
						}
						if (!isSpace) {
							pos = 0;
						}
							if (pos == 0) {
							nextLine = nextLine.substr(0, pos) + getLastWord(newstring) + "  ";
						}
						else {
							nextLine = nextLine.substr(0, pos) + " " + getLastWord(newstring) + "  ";
						}
					}
				} else if ((name == "?") && !successMatch && !previousList.empty() && !nextWordHelp) {
					nextLine = command;
				}
				if (currentDir == "error" && currentMode != mode.globalConfiguration && currentMode != mode.userExec && currentMode != mode.privilegedExec && !help && Functions::lowerCase(command) != "exit") {
				globalCommand = true;
				string prevMode = currentMode;
				nlohmann::json prevJson = jsonDir;
				pugi::xml_node prevXML = config_node;
				switchMode(mode.globalConfiguration);
				historyToGlobal();
				string nextCommand = command;
				Process(nextCommand);
				currentDir.clear();
				if (currentMode == mode.globalConfiguration) {
					if (successCommand) {
						return "error";
					} else {
						switchMode(prevMode);
						config_node = prevXML;
						jsonDir = prevJson;
						if (successCommand) {
							return "error";
						}
					}
				}
				else {
					return "error";
				}
				
				} 
				if (currentDir == "error" && !isGlobal(name) && !help) {
					run = 0;
					cout << endl;
					std::string hostname = Global::getInstance().Hostname();
					for (char i : hostname) {
						cout << " ";
					}
					for (char i : currentMode) {
						cout << " ";
					}
					for (char i : oldstring) {
						cout << " ";
					}
					cout << " ^" << endl;
					cout << "% Invalid input detected at '^' marker." << endl;
				}

				m = 0;

				bool done = 0;
				if (!done) {
					vector<com> matches;
					for (const auto& word : commandlist) {
						if (matchPattern && word.name == pattern) {
							matches.push_back(word);
						}
						if (word.name.size() >= name.size()) {
							if (equal(name.begin(), name.end(), Functions::lowerCase(word.name).begin())) {
								matches.push_back(word);
							}
						}
					}
					previousList = matches;
					if (previousList.empty()) {
						previousList = commandlist;
					}
					if (name == "?" && (successMatch || nextWordHelp) && !endcommand) {
						printNames(commandlist);
						newstring += name;
						volitileString += name;
						nextLine = oldstring + " ";
						if (commandlist[0].name != "<error>") {
							//cout << nextWordHelp;
						} else {
							nextLine = command;
						}
						if (nextWordHelp) {
							nextLine += " ";
						}
					} else if (name == "vk_tab" && (commandlist[0].name == "<error>" || nextWordHelp)) {
						nextLine = command;
					}
					successMatch = 0;
					if (previousList.size() == 1 && !matches.empty()) {
						if (matchPattern && pattern == matches[0].name) {
							successMatch = 1;
						}
						if (matches[0].name == name) {
							successMatch = 1;
						}
					} else if (name == "?" && (successMatch || nextWordHelp)) {
						nextLine = command;
					}
					if (!done && matches.size() <= 1) {
						if (matchPattern) {
							newstring = newstring + " " + normalStream[i];
							oldstring = oldstring + " " + normalStream[i];
							volitileString = volitileString + " " + pattern;
							previousMatchString = matches[0].name;
						} else if (matches.size() == 1) {
							newstring = newstring + " " + matches[0].name;
							oldstring = oldstring + " " + name;
							volitileString = volitileString + " " + name;
							done = true;
							previousMatchString = matches[0].name;
						} else if (matches.empty() && endcommand) {
							newstring = newstring + " " + endstring;
							oldstring = oldstring + " " + name;
							volitileString = volitileString + " " + name;
							previous = name;
						} else if (matches.empty() ) {
							newstring = newstring + " " + name;
							oldstring = oldstring + " " + name;
							volitileString = volitileString + " " + name;
							previous = name;
							if (!help) {
								return name;
							}
						} else {
							newstring = newstring + " " + matches[0].name;
							oldstring = oldstring + " " + name;
							volitileString = volitileString + " " + name;
							done = true;
							previousMatchString = matches[0].name;
						}
					} else {
						oldstring = oldstring + " " + name;
						volitileString = volitileString + " " + name;
					}
				}
			}
		}
		i++;
		previous = name;
	}
	oldstring = format(oldstring);
	volitileString = format(volitileString);
	oldCommandStream = extractWords(volitileString);
	newstring = format(newstring);
	nextLine = format(nextLine);
	if (!validCommand && !help && !matchPatternEnd && !line) {
		cout << endl;
		cout << "Incomplete Command";
		return "";
	} else {
		return newstring;
	 }
}

vector<com> Terminal::GetCommandList(const nlohmann::json& execCommands, const string& directory, bool mode) {
	
	vector<com> commandlist;
	nlohmann::json currentDirCopy = currentDir;
	vector<com> noSubComList{er};
	com equalWord;
	bool isEqual = false;
	bool valid = false;

	if (currentDir == "error") {
		return noSubComList;
	}

	nlohmann::json w;
	int matches {0};
	currentDirCopy = currentDir;
	for (auto& work : currentDirCopy) {
		com word;
		word.name = work["name"];
		word.description = work["description"];
		commandlist.push_back(word);
	}

	for (auto& work : currentDirCopy) {

		string word = work["name"];
		if (matchesPattern(directory, word) && !endcommand) {
			w = work;
			matches++;
			if (!isValidDirectory(w)) {
				endcommand = true;	
			}
		} else if (word.size() >= directory.size()) {
			if (equal(directory.begin(), directory.end(), Functions::lowerCase(word).begin()) && !isEqual) {
				w = work;
				matches++;
			}
			if (word == directory) {
				isEqual = true;
				com wr;
				wr.name = Functions::lowerCase(work["name"]);
				wr.description = work["description"];
				equalWord = wr;
				w = work;
			}
		}
	}
	if (isEqual) {
		matches = 1;
	}
	if (matches == 1 && isValidDirectory(w)) {
		currentDir = w["subcommands"];
		for (auto& word : currentDir) {
			if (word["name"] == "<cr>") {
				validCommand = true;
				valid = true;
			}
		}
		if (!valid) {
			validCommand = false;
		}
		if (isEqual) {
			commandlist.clear();
			commandlist.push_back(equalWord);
		}
	}
	else if ((isValidDirectory(w) || matches != 1) && !successMatch) {
		currentDir = "error";
	} else if (isEqual && !isValidDirectory(w) && !directory.empty()) {
		endstring = Functions::lowerCase(w["name"]);
		endcommand = true;
		return noSubComList;
	}
	if (matches == 0 && !directory.empty() && currentDir == "error" && directory != "?" && directory != "vk_tab") {
		return noSubComList;
	}
	if (matches == 0 && !isValidDirectory(w) && directory != "?" && directory != "vk_tab" && !matchPattern) {
		currentDir = "error";
		return noSubComList;
	}
	return commandlist;
}

bool Terminal::isGlobal(string& name) {
	for (string i : globalList) {
		if (i.size() >= name.size() && equal(name.begin(), name.end(), i.begin())) {
			return true;
		}
	}
	return false;
}

void Terminal::printNames(vector<com> list) {
	uint8_t size = 0;
	int line = 0;
	for (com i : list) {
		if (i.name.size() > size) {
			size = i.name.size();
		}
	}
	for (com i : list) {
		if (i.name != "<error>") {
			if (more(line)) {
				cout << "\n  " << i.name;
				int siz = i.name.size();
				for (int i = 0; i <= (size - siz + 5); i++) {
					cout << " ";
				}
				//cout << i.description;
				line++;
			} else {
				return;
			}
		} else {
			return;
		}
	}
}

std::string Terminal::getLastWord(const std::string& inputString) {
    std::istringstream stream(inputString);
    std::string stringword;
    std::string stringlastWord;
    while (stream >> stringword) {
        stringlastWord = stringword;
    }

    return stringlastWord;
}

std::vector<std::string> Terminal::extractWords(const std::string& str) {
    std::vector<std::string> words;
    std::string word;
    bool inWord = false;
	bool space = false;

	for (char ch : str) {
		if (!isspace(ch) && ch != '?' && ch != '\t') {
			word += ch;
			inWord = true;
			space = false;
		}
		else if (ch == '?') {
			if (!word.empty()) {
				words.push_back(word);
			}
			word = ch;
			if (space) {
				nextWordHelp = true;
			}
			space = false;
		}
		else if (ch == '\t') {
			if (!word.empty()) {
				words.push_back(word);
			}
			word = "vk_tab";
			if (space) {
				nextWordHelp = true;
			}
			space = false;
		}
		else if (inWord) {
			words.push_back(word);
			word.clear();
			inWord = false;
			space = true;
		}
	}
	if (!word.empty()) {
		words.push_back(word);
	}
	return words;
}

string Terminal::format(string str) {
	string newstr = str;
	for (int ch = 0; ch <= str.size(); ch++) {
		if (isspace(str[ch])) {
			newstr = newstr.substr(1);
		} else {
			break;
		}
	}
	return newstr;
}

bool Terminal::matchesPattern(const std::string& input, const std::string& regexRange) {
	if (regexRange == "WORD" && input != "?" && input != "vk_tab") {
		pattern = regexRange;
		matchPattern = true;
		return true;
	}
	if (regexRange == "LINE" && input != "?" && input != "vk_tab") {
		pattern = regexRange;
		matchPattern = true;
		line = true;
		return true;
	}
	if (regexRange == "A.B.C.D" && input != "?" && input != "vk_tab") {
		int oct1, oct2, oct3, oct4;
	#ifdef _WIN32
		sscanf_s(input.c_str(), "%d.%d.%d.%d", &oct1, &oct2, &oct3, &oct4);
	#else
		sscanf(input.c_str(), "%d.%d.%d.%d", &oct1, &oct2, &oct3, &oct4);
	#endif
		vector<int> oct{oct1, oct2, oct3, oct4};
		bool isIP = true;
		for (int i : oct) {
			if (i < 0 || i > 255) {
				isIP = false;
			}
		}
		if (isIP) {
			pattern = regexRange;
			matchPattern = true;
			return true;
		}
	}
	if (regexRange == "X:X:X:X::X") {
		if (isValidIPv6(input)) {
			pattern = regexRange;
			matchPattern = true;
			return true;
		}
	}
	if (regexRange == "X:X:X:X::X/<0-128>") {
		if (isValidIPv6WithMask(input)) {
			pattern = regexRange;
			matchPattern = true;
			return true;
		}
	}
	if (regexRange == "H.H.H") {
		if (isValidMACAddress(input)) {
			pattern = regexRange;
			matchPattern = true;
			return true;
		}
	}
	if (regexRange == "x/y/z") {}

	if (regexRange[0] == '<') {
		int min, max;
	#ifdef _WIN32
		sscanf_s(regexRange.c_str(), "<%d-%d>", &min, &max);
	#else
		sscanf(regexRange.c_str(), "<%d-%d>", &min, &max);
	#endif
		if (isNumber(input)) {
			if (std::stoi(input) >= min && std::stoi(input) <= max) {
				pattern = regexRange;
				matchPattern = true;
				matchPatternEnd = true;
				return true;
			}
		}
	}
	return false;
}

bool Terminal::isNumber(const std::string& s) {
    if (s.empty() || ((!isdigit(s[0])) && (s[0] != '-') && (s[0] != '+'))) return false;

    char* p;
    strtol(s.c_str(), &p, 10);

    return (*p == 0);
}

bool Terminal::isValidDirectory(nlohmann::json& js) {
	if (!js.contains("subcommands")) {
		return false;
	} else {
		return true;
	}
}

bool Terminal::more(int& lineNum) {
	if (lineNum % 10 == 0 && lineNum != 0) {
		cout << "\n  --More--";
	#ifdef _WIN32
		HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    	DWORD mode;
    	GetConsoleMode(hInput, &mode);
    	SetConsoleMode(hInput, mode & (~ENABLE_PROCESSED_INPUT));

		DWORD read;
    	INPUT_RECORD ir;
    	DWORD written;

		while (true) {

	        maxCommandLength = getTerminalWidth() - initialLineLength;

	        ReadConsoleInput(hInput, &ir, 1, &read);

	        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
	            if (ir.Event.KeyEvent.wVirtualKeyCode == VK_SPACE) {
					while (getCursorPosition().X != 0) {
						moveCursorLeft(1);
						cout << " ";
						moveCursorLeft(1);
					}
					moveCursorLeft(1);
					return true;
				} else if (ir.Event.KeyEvent.uChar.AsciiChar == 'q') {
					while (getCursorPosition().X != 0) {
						moveCursorLeft(1);
						cout << " ";
						moveCursorLeft(1);
					}
					moveCursorLeft(1);
					return false;
				}
			}
		}
	#else



		while (true) {

	        maxCommandLength = getTerminalWidth() - initialLineLength;

	        if (kbhit()) {
				char nextch = getchar();
	            if (nextch == '\x20') {
					while (getCursorPosition().col != 1) {
						moveCursorLeft(1);
						cout << " ";
						moveCursorLeft(1);
					}
					moveCursorLeft(1);
					return true;
				} else if (nextch == 'q') {
					while (getCursorPosition().col != 1) {
						moveCursorLeft(1);
						cout << " ";
						moveCursorLeft(1);
					}
					moveCursorLeft(1);
					return false;
				}
			}
		}

	#endif
	} else {
		return true;
	}
}

void Terminal::switchMode(string& newMode) {
	prevMode = currentMode;
	currentMode = newMode;
	jsonDir = json[currentMode];
	modeChange = true;
}

std::vector<std::string> Terminal::split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string Terminal::fillZeros(const std::string& str) {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << str;
    return oss.str();
}

std::string Terminal::convertToFullIPv6(const std::string& ipv6) {
	string ip, prefix;
	bool hasPrefix = false;
	for (size_t i = 0; i < ipv6.length(); i++) {
		if (ipv6[i] == '/') {
			ip = ipv6.substr(0, i - 1);
			prefix = ipv6.substr(i);
			hasPrefix = true;
		}
	}
	if (!hasPrefix) {
		ip = ipv6;
	}
    std::string expandedIPv6 = ip;
    size_t doubleColonPos = expandedIPv6.find("::");
    if (doubleColonPos != std::string::npos) {
        std::vector<std::string> frontParts = split(expandedIPv6.substr(0, doubleColonPos), ':');
        std::vector<std::string> backParts = split(expandedIPv6.substr(doubleColonPos + 2), ':');
        int hextetCount = frontParts.size() + backParts.size();
        std::string zeros = "";
        for (int i = 0; i < 8 - hextetCount; ++i) {
            zeros += "0000:";
        }
        expandedIPv6 = "";
        for (const std::string& part : frontParts) {
            expandedIPv6 += fillZeros(part) + ":";
        }
        expandedIPv6 += zeros;
        for (const std::string& part : backParts) {
            expandedIPv6 += fillZeros(part) + ":";
        }
        if (!expandedIPv6.empty() && expandedIPv6.back() == ':') {
            expandedIPv6.pop_back();
        }
    } else {
        std::vector<std::string> parts = split(expandedIPv6, ':');
        expandedIPv6 = "";
        for (const std::string& part : parts) {
            expandedIPv6 += fillZeros(part) + ":";
        }
        if (!expandedIPv6.empty() && expandedIPv6.back() == ':') {
            expandedIPv6.pop_back();
        }
    }
    return expandedIPv6 + prefix;
}

bool Terminal::isValidIPv6(const std::string& ipv6) {
    std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))");
    return std::regex_match(ipv6, ipRegex);
}

bool Terminal::isValidIPv6WithMask(const std::string& ipWithMask) {
    std::regex ipRegex("((([0-9A-Fa-f]{1,4}):){7}([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,7}:|(([0-9A-Fa-f]{1,4}):){1,6}:([0-9A-Fa-f]{1,4})|(([0-9A-Fa-f]{1,4}):){1,5}((:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}):){1,4}((:[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}):){1,3}((:[0-9A-Fa-f]{1,4}){1,4})|(([0-9A-Fa-f]{1,4}):){1,2}((:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}):((:[0-9A-Fa-f]{1,4}){1,6})|:((:[0-9A-Fa-f]{1,4}){1,7}|:)|fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9A-Fa-f]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))/(12[0-8]|1[01][0-9]|[1-9]?[0-9])");
    return std::regex_match(ipWithMask, ipRegex);
}

bool Terminal::isValidMACAddress(const std::string& mac) {
    std::regex macRegex("^([0-9A-Fa-f]{1,4}[:-]?){6}([0-9A-Fa-f]{1,4})$");
    return std::regex_match(mac, macRegex);
}

void Terminal::getInterfaceMode(string& type) {
	if (type == "Dialer") {switchMode(mode.dialer); currentSubMode = type;}
	else if (type == "Ethernet") {switchMode(mode.ethernet); Interfaces = &InterfaceList["EthernetList"]; currentSubMode = type;}
	else if (type == "FastEthernet") {switchMode(mode.fastEthernet); Interfaces = &InterfaceList["FastEthernetList"]; currentSubMode = type;}
	else if (type == "GigabitEthernet") {switchMode(mode.gigabitEthernet); Interfaces = &InterfaceList["GigabitList"]; currentSubMode = type;}
	else if (type == "Loopback") {switchMode(mode.loopback); Interfaces = &InterfaceList["LoopbackList"]; currentSubMode = type;}
	else if (type == "Portchannel") {switchMode(mode.portchannel); Interfaces = &InterfaceList["PortchannelList"]; currentSubMode = type;}
	else if (type == "Tunnel") {switchMode(mode.tunnel); Interfaces = &InterfaceList["TunnelList"]; currentSubMode = type;}
	else if (type == "Virtual-Template") {switchMode(mode.virtualTemplate); Interfaces = &InterfaceList["VirtualTemplateList"]; currentSubMode = type;}
	else if (type == "Vlan") {switchMode(mode.vlan); Interfaces = &InterfaceList["VlanList"]; currentSubMode = type;}
	jsonDir = jsonDir[0][type];
}

void Terminal::getRoutingMode(string& type) {
	if (type == "bgp") {switchMode(mode.bgp); currentSubMode = type;} 
	else if (type == "eigrp_classic") {switchMode(mode.eigrp_classic); currentSubMode = type;} 
	else if (type == "eigrp_named") {switchMode(mode.eigrp_named); currentSubMode = type;} 
	else if (type == "ospf") {switchMode(mode.ospf); currentSubMode = type;}
	else if (type == "rip") {switchMode(mode.rip); currentSubMode = type;}
	jsonDir = jsonDir[0][type];
}

