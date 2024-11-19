#include <Terminal.h>

void Terminal::Process(string& command) {

	modeChange = false;
	string preProcessMode = currentMode;

	command = FixCommand(command);
	successCommand = false;
		
	vector<string> TEMPcommandStream = extractWords(command);
	vector<string> commandStream;
	
	bool textLine = false;
	for (unsigned long index = 0; index < TEMPcommandStream.size(); index++)  {
		if (!textLine) {
			commandStream.push_back(TEMPcommandStream[index]);
		} else {
			commandStream[commandStream.size() - 1] += " " + TEMPcommandStream[index];
		}
		if (index > 0) {
			if (oldCommandStream[index] == "LINE") {
				textLine = true;
			}
		}
	}

	
	if (commandStream.empty() || help || !run) {
		return;
	}

	if (!help && (validCommand || matchPattern)) {
		//cout << "\n" << command;
	}

	if (command == "end" && currentMode != mode.userExec) {
		while (currentMode != mode.privilegedExec) {
			string exit = "exit";
			Process(exit);
		}
	}

	successCommand = true;

	if (!no) {

	    if (currentMode == mode.userExec || currentMode == mode.privilegedExec) {
	        if (command == "show history") {for (std::string str : history) {if (str != "") {cout << "\n  " + str;}}}
			if (command == "show clock") {cout << "\n" << time.GetTime();}
			if (command == "write memory") {
				if (!doc.save_file("../VirtualRouter/Dir/startup-config.xml")) {
	        		std::cerr << "Error saving XML file" << std::endl;
	    		}
			}
	    }


		if (currentMode == mode.userExec) {
			if (command == "enable") {switchMode(mode.privilegedExec);}
			if (command == "exit") {exit(1);}
		}


		if (currentMode == mode.privilegedExec) {
			if (command == "configure terminal") {switchMode(mode.globalConfiguration); cout << "\nEnter configuration commands, one per line.  End with CNTL/Z.";}
			if (command == "exit") {switchMode(mode.userExec);}
		}


		if (currentMode == mode.globalConfiguration) {
			if (command == "exit") {switchMode(mode.privilegedExec);}
			if (commandStream[0] == "hostname") {
				Global::getInstance().Hostname() = commandStream[1];
			}	
			if (commandStream[0] == "interface") {
				string type = commandStream[1]; 
				string interfaceID_temp = commandStream[2];
				interfaceID = static_cast<unsigned long>(Functions::stringToNum(commandStream[2]));
				string intType;
				if ((type == "Ethernet" || type == "GigabitEthernet" || type == "FastEthernet") && physicalInterfaces.size() >= interfaceID) {
					intType = physicalInterfaces[interfaceID];
				} else {
					intType = "NO_INTERFACE";
				}
				string mac;
				if (commandStream[1] == "Ethernet" && macAddressList.Ethernet.size() == 9) {
					mac = OUI + macAddressList.Ethernet[interfaceID];
				} else if (commandStream[1] == "FastEthernet" && macAddressList.FastEthernet.size() == 9) {
					mac = OUI + macAddressList.FastEthernet[interfaceID];
				} else if (commandStream[1] == "GigabitEthernet" && macAddressList.GigabitEthernet.size() == 9) {
					mac = OUI + macAddressList.GigabitEthernet[interfaceID];
				} else if (commandStream[1] == "Dot11Radio") {
					mac = OUI + "0d";
					char buffer[5];
	    			std::sprintf(buffer, "%04ld", interfaceID);
					mac += buffer;
				}
				getInterfaceMode(type);
				if (Interfaces->count(interfaceID) == 0) {
					(*Interfaces)[interfaceID] = std::make_shared<Interface>(intType, 1024, 1024, mac, interfaceID, debug);
				}
				CurrentInterface = Interfaces->at(interfaceID).get();
			}
			if (commandStream[0] == "router") {
				string type = commandStream[1];
				string ID;
				if (type != "rip") ID = commandStream[2];
				if (type != "rip") routingProtocolID = Functions::stringToNum(commandStream[2]);
				if (type == "eigrp") {
					if (Functions::isDecimal(ID)) {type = "eigrp_classic";}
					else {type = "eigrp_named";}
				}
				getRoutingMode(type);
				if (type == "eigrp_classic") {
					if (eigrpList.count(routingProtocolID) == 0) {
						(eigrpList)[routingProtocolID] = std::make_shared<Protocol::Eigrp>(routingProtocolID);
					}
					currentEigrp = eigrpList.at(routingProtocolID).get();
				} else if (type == "ospf") {
					if (OspfList.count(routingProtocolID) == 0) {
						(OspfList)[routingProtocolID] = std::make_shared<Protocol::Ospf>();
					}
					CurrentOspf = OspfList.at(routingProtocolID).get();
				} else if (type == "bgp") {
					if (BgpList.count(routingProtocolID) == 0) {
						(BgpList)[routingProtocolID] = std::make_shared<Protocol::Bgp>();
					}
				}

			}
		}

		if (currentMode == "(config-if)#") {
			if (command == "exit") {switchMode(mode.globalConfiguration);}
			if (commandStream[0] == "ip" && commandStream[1] == "address") {
				if (commandStream[2] != "dhcp") {
					CurrentInterface->setIPv4(Functions::addressToByte(commandStream[2]), Functions::addressToByte(commandStream[3]));
				} else {
					thread dhcpThread([this]() {
						this->runDhcp();
					});
					dhcpThread.detach();
				}
			}
		}

		if (currentMode == "(config-router)#") {
			if (currentSubMode == "eigrp_classic") {
				if (commandStream[0] == "network") {
					EigrpConfigs::network network;
					network.ip = Functions::addressToByte(commandStream[1]);
					if (commandStream.size() == 3) {
						network.mask = Functions::addressToByte(commandStream[2]);
					} else {
						network.mask = variable.ip.broadcast;
					}
					currentEigrp->AddNetwork(network);
					currentEigrp->UpdateInterfaceList();
					currentEigrp->UpdateRoutingTableForConnected();
				}
			} else if (currentSubMode == "ospf") {

			} else if (currentSubMode == "bgp") {

			} else if (currentSubMode == "rip") {

			}
		}
	}
 

	bool notlist = false;
	if (notlist) {
		listHistory.push_back(command);
	}
	if (commandStream[0] == "ip" && (commandStream[1] == "route")) {notlist = true;}

	if (preProcessMode != mode.userExec && preProcessMode != mode.privilegedExec && command != "error") {
		Functions::printVector(oldCommandStream);
		//cout << "\n" << endl;
		Functions::printVector(commandStream);
		saveCommand(oldCommandStream, commandStream, modeChange, notlist);
	}

}

void Terminal::runDhcp() {
	std::string mac = CurrentInterface->Get().mac;

	CurrentInterface->dhcp->InitializeDhcp(mac);

}