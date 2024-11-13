#pragma once

#include <Time.h>
#include "Interface.h"
#include <Eigrp.h>
#include <Ospf.h>
#include <Rip.h>
#include <Bgp.h>

#include <Functions.h>
#include "Console.h"

using namespace std;

class Terminal : public Console {
public:
	com er, cr;


	Terminal(bool isDebug = false);
	void Input();
	
private:

	Variable variable;

	void Process(string& command);
	void UnProcess(string& command);
	string FixCommand(const string& command);
	vector<com> GetCommandList(const nlohmann::json& execCommands, const string& directory, bool mo);
	bool isGlobal(string& name);
	void printNames(vector<com> list);
	string getLastWord(const std::string& inputString);
	vector<string> extractWords(const std::string& str);
	string format(string str);
	bool matchesPattern(const std::string& input, const std::string& regexRange);
	bool isNumber(const std::string& s);
	bool isValidDirectory(nlohmann::json& js);
	bool more(int& lineNum);
	void switchMode(string& newMode);
	std::string fillZeros(const std::string& str);
	std::string convertToFullIPv6(const std::string& ipv6);
	std::vector<std::string> split(const std::string& str, char delimiter);
	bool isIPV6(string& ip);
	bool isValidIPv6(const std::string& ipv6);
	bool isValidIPv6WithMask(const std::string& ipWithMask);
	bool isValidMACAddress(const std::string& mac);
	void getInterfaceMode(string& type);
	void getRoutingMode(string& type);
	void recover();

	// threads

	void runDhcp();
	void runEigrp();
	void runOspf();
	void runBgp();
	void runRip();

	unsigned long interfaceID;
	int routingProtocolID;
	
	map<int, std::shared_ptr<Interface>>* Interfaces;

	vector<string> globalList{"exit", "end", "?", "vk_tab"};
	vector<string> oldCommandStream;

	DoTime time;

    string pattern;
	string endstring;
	string previousMatchString;
	string current;
	string currentSubMode;

	nlohmann::json json;
	nlohmann::json currentDir;
	nlohmann::json jsonDir;

	vector<string> listHistory;

	bool run = true;
	bool endcommand = false;
	bool nextWordHelp = false;
	bool successMatch = false;
	bool line = false;
	bool help = false;
	bool validCommand = false;
	bool matchPattern = false;
	bool matchPatternEnd = false;
	bool modeChange = false;
	bool successCommand = false;
	bool globalCommand = false;

	condition_variable cv;

	bool debug;
};
