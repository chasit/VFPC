#include "stdafx.h"
#include "analyseFP.hpp"

extern "C" IMAGE_DOS_HEADER __ImageBase;

bool blink;
bool debugMode, initialSidLoad;

int disCount;

char DllPathFile[_MAX_PATH];
string sidJsonFileLocation;

vector<string> AircraftIgnore;
json sidData;

using namespace std;
using namespace EuroScopePlugIn;

// Run on Plugin Initialization
VFPCPlugin::VFPCPlugin(void) : CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, MY_PLUGIN_NAME, MY_PLUGIN_VERSION, MY_PLUGIN_DEVELOPER, MY_PLUGIN_COPYRIGHT)
{
	string loadingMessage = "Version: ";
	loadingMessage += MY_PLUGIN_VERSION;
	loadingMessage += " loaded.";
	sendMessage(loadingMessage);

	// Register Tag Item "VFPC"
	RegisterTagItemType("VFPC", TAG_ITEM_FPCHECK);
	RegisterTagItemType("VFPC (if failed)", TAG_ITEM_FPCHECK_IF_FAILED);
	RegisterTagItemType("VFPC (if failed, static)", TAG_ITEM_FPCHECK_IF_FAILED_STATIC);
	RegisterTagItemFunction("Check FP", TAG_FUNC_CHECKFP_MENU);

	// Get Path of the Sid.json
	GetModuleFileNameA(HINSTANCE(&__ImageBase), DllPathFile, sizeof(DllPathFile));
	sidJsonFileLocation = DllPathFile;
	sidJsonFileLocation.resize(sidJsonFileLocation.size() - strlen("VFPC.dll"));
	sidJsonFileLocation += "Sids.json";

	debugMode = false;
	initialSidLoad = false;

	// Remove the log file on plugin load
	std::string logPath = DllPathFile;
	size_t lastSlash = logPath.find_last_of("/\\");
	if (lastSlash != std::string::npos)
		logPath = logPath.substr(0, lastSlash + 1);
	else
		logPath = "";
	logPath += "VFPC.log";
	std::remove(logPath.c_str());

	logToFile("Loaded the configuration!");
}

// Run on Plugin destruction, Ie. Closing EuroScope or unloading plugin
VFPCPlugin::~VFPCPlugin()
{
}

void VFPCPlugin::debugMessage(string message)
{
	// Display Debug Message if debugMode = true
	if (debugMode)
	{
		DisplayUserMessage("VFPC", "Debug", message.c_str(), true, true, true, false, false);
		logToFile(message);
	}
}

void VFPCPlugin::sendMessage(string type, string message)
{
	// Show a message
	DisplayUserMessage("VFPC", type.c_str(), message.c_str(), true, true, true, true, false);
	if (debugMode)
	{
		logToFile(message);
	}
}

void VFPCPlugin::sendMessage(string message)
{
	DisplayUserMessage("Message", "VFPC", message.c_str(), true, true, true, false, false);
	if (debugMode)
	{
		logToFile(message);
	}
}

// Logs a message to a file in the same folder as the DLL
void VFPCPlugin::logToFile(const std::string& message)
{
	// Get the DLL directory from DllPathFile (already set at startup)
	std::string logPath = DllPathFile;
	size_t lastSlash = logPath.find_last_of("/\\");
	if (lastSlash != std::string::npos)
	{
		logPath = logPath.substr(0, lastSlash + 1);
	}
	else
	{
		logPath = "";
	}
	logPath += "VFPC.log";

	// Get current time
	auto now = std::chrono::system_clock::now();
	auto in_time_t = std::chrono::system_clock::to_time_t(now);
	std::tm buf;
#ifdef _WIN32
	localtime_s(&buf, &in_time_t);
#else
	localtime_r(&in_time_t, &buf);
#endif
	char timeStr[32];
	std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &buf);

	std::ofstream logfile(logPath, std::ios_base::app);
	if (logfile.is_open())
	{
		logfile << "[" << timeStr << "] " << "Debug: " << message << std::endl;
	}
}

// Parses the Sid.json file
void VFPCPlugin::getSidData()
{
	stringstream ss;
	ifstream ifs(sidJsonFileLocation.c_str(), ios::binary);
	sidData = json::parse(ifs);
}

void VFPCPlugin::validateSid(
	CFlightPlan flightPlan, ValidationContext& ctx, map<string, string>& returnValid)
{
	returnValid["CS"] = flightPlan.GetCallsign();
	returnValid["STATUS"] = "Passed";
	bool valid{ false };

	string origin = flightPlan.GetFlightPlanData().GetOrigin();
	boost::to_upper(origin);
	string destination = flightPlan.GetFlightPlanData().GetDestination();
	boost::to_upper(destination);
	int RFL = flightPlan.GetFlightPlanData().GetFinalAltitude();
	int requestedFlightLevel = RFL / 1000;

	vector<string> route_tokens = split(flightPlan.GetFlightPlanData().GetRoute(), ' ');
	for (std::size_t i = 0; i < route_tokens.size(); i++)
	{
		boost::to_upper(route_tokens[i]);
	}

	if (strcmp(flightPlan.GetFlightPlanData().GetPlanType(), "V") > -1)
	{
		returnValid["SEARCH"] = "VFR Flight, no SID required!";
		returnValid["STATUS"] = "Passed";
		return;
	}

	string sid_name = flightPlan.GetFlightPlanData().GetSidName();
	boost::to_upper(sid_name);

	// Flightplan has SID
	if (!sid_name.length())
	{
		returnValid["SEARCH"] = "Flight plan doesn't have SID set!";
		returnValid["STATUS"] = "Failed";
		ctx.fail(ValidationCheck::SID_ERROR);
		return;
	}

	if (sid_name == "DIFT")
	{
		returnValid["SEARCH"] = "DIFT Set, not checking the flight plan!";
		returnValid["STATUS"] = "Passed";
		return;
	}

	string first_wp = sid_name.substr(0, sid_name.find_first_of("0123456789"));
	if (0 != first_wp.length())
		boost::to_upper(first_wp);
	string sid_suffix;
	if (first_wp.length() != sid_name.length())
	{
		sid_suffix = sid_name.substr(sid_name.find_first_of("0123456789"), sid_name.length());
		boost::to_upper(sid_suffix);
	}

	// Did not find a valid SID
	if (sid_suffix.length() == 0 && "VCT" != first_wp)
	{
		returnValid["SEARCH"] = "Flight plan doesn't have SID set!";
		returnValid["STATUS"] = "Failed";
		ctx.fail(ValidationCheck::SID_ERROR);
		return;
	}

	std::string resolved_sid = first_wp;

	auto data = sidData;

	// Check if SID needs to be mapped
	if (data.contains("sid_mapping") && data["sid_mapping"].contains(first_wp))
	{
		resolved_sid = data["sid_mapping"][first_wp];
	}

	// Locate SID details for the given ICAO
	const auto& sid_details = data["sid_details"];
	const auto sid_entry = std::find_if(
		sid_details.begin(), sid_details.end(),
		[&](const json& entry)
		{
			return entry.value("icao", "") == origin;
		});

	if (sid_entry == sid_details.end())
	{
		returnValid["SEARCH"] = "Invalid SID, no SID details found for " + origin;
		returnValid["STATUS"] = "Failed";
		ctx.fail(ValidationCheck::SID_ERROR);
		return;
	}

	const auto& sids = sid_entry->at("sids");
	if (!sids.contains(resolved_sid))
	{
		returnValid["SEARCH"] = "Invalid SID, SID " + first_wp + " not listed for " + origin;
		returnValid["STATUS"] = "Failed";
		ctx.fail(ValidationCheck::SID_ERROR);
		return;
	}

	returnValid["SID"] = resolved_sid;

	const auto& sid_def = sids[resolved_sid][0];

	// Check direction (ODD/EVEN)
	if (sid_def.contains("direction"))
	{
		std::string direction = sid_def["direction"];
		bool is_even = (requestedFlightLevel % 2 == 0);

		if ((direction == "ODD" && is_even) || (direction == "EVEN" && !is_even))
		{
			ctx.fail(ValidationCheck::LEVEL_ERROR);
			returnValid["DIRECTION"] = "Failed " + direction;
		}
		else
		{
			returnValid["DIRECTION"] = "Passed " + direction;
		}
	}

	if (sid_def.contains("destinations"))
	{
		const auto& destinations = sid_def["destinations"];
		if (destinations.is_array() && !destinations.empty())
		{
			bool destination_found = std::any_of(destinations.begin(), destinations.end(),
				[&destination](const json& dest)
				{
					return dest.get<std::string>() == destination;
				});

			if (!destination_found)
			{
				returnValid["DESTINATION"] = "Failed SID not valid for destination " + destination;
				ctx.fail(ValidationCheck::SID_ERROR);
			}
			else
			{
				returnValid["DESTINATION"] = "Passed, SID valid for destination " + destination;
			}
		}
	}
	else
	{
		returnValid["DESTINATION"] = "No destination restraint";
	}

	// Check airway requirement: only perform check if airway_required does not exist or is not set to false
	// If the route is just 1 it's probably a route that goes within the EHAA FIR.
	if ((!sid_def.contains("airway_required") || sid_def["airway_required"] != false) && route_tokens.size() > 1)
	{
		// Very basic airway check: airway tokens are usually alphanumeric (e.g., "UL620", "N198")
		string first_token = route_tokens[1];
		logToFile("Debug: " + first_token);
		bool looks_like_airway = std::any_of(first_token.begin(), first_token.end(), ::isalpha) &&
			std::any_of(first_token.begin(), first_token.end(), ::isdigit);

		if (!looks_like_airway)
		{
			returnValid["AIRWAYS"] = "Failed airway requirement after exit point, but '" + first_token + "' does not look like an airway";
			returnValid["STATUS"] = "Failed";
			ctx.fail(ValidationCheck::ROUTE_ERROR);
		}
		else
		{
			returnValid["AIRWAYS"] = "Passed airway requirement";
		}
	}
	else
	{
		returnValid["AIRWAYS"] = "Passed airway requirement";
	}

	// return "valid SID: SID checks passed";
	if (debugMode){
		logToFile("Last callsign: " + returnValid["CS"] + ", SID: " + sid_name + ", Origin: " + origin + ", Destination: " + destination + ". Attempting to search restrictions...");
	}
}

void VFPCPlugin::searchRestrictions(
	CFlightPlan flightPlan, ValidationContext& ctx, map<string, string>& returnValid)
{
	auto data = sidData;
	if (!data.contains("restrictions") || !data["restrictions"].is_array())
	{
		returnValid["SEARCH"] = "No restrictions found!";
		return;
	}
	string origin = flightPlan.GetFlightPlanData().GetOrigin();
	boost::to_upper(origin);
	string destination = flightPlan.GetFlightPlanData().GetDestination();
	boost::to_upper(destination);
	int RFL = flightPlan.GetFlightPlanData().GetFinalAltitude();
	int requestedFlightLevel = RFL / 100;

	vector<string> route_tokens = split(flightPlan.GetFlightPlanData().GetRoute(), ' ');
	for (std::size_t i = 0; i < route_tokens.size(); i++)
	{
		boost::to_upper(route_tokens[i]);
	}

	std::optional<int> min_capping;

	for (const auto& restriction : data["restrictions"])
	{
		std::string id = restriction.value("id", "");
		// "ALL" restriction check
		if (id == "ALL")
		{
			const auto& from_list = restriction["From"];
			if (std::find(from_list.begin(), from_list.end(), origin) != from_list.end())
			{
				if (restriction.contains("forbidden_fls"))
				{
					int forbidden = restriction["forbidden_fls"];
					if (requestedFlightLevel == forbidden)
					{
						returnValid["FORBIDDEN_FL"] = "Failed forbidden FL";
						ctx.fail(ValidationCheck::LEVEL_ERROR);
					}
					else
					{
						returnValid["FORBIDDEN_FL"] = "Passed forbidden FL";
					}
				}
			}
			continue;
		}

		// Match departure ICAO
		bool matches_from = false;
		if (restriction.contains("From"))
		{
			for (const auto& from : restriction["From"])
			{
				if (from == origin)
				{
					matches_from = true;
					break;
				}
			}
		}
		else if (restriction.contains("from"))
		{
			for (const auto& from : restriction["from"])
			{
				if (from == origin)
				{
					matches_from = true;
					break;
				}
			}
		}
		if (!matches_from)
			continue;
		if (!restriction.contains("routes") || !restriction["routes"].is_array())
			continue;

		for (const auto& route : restriction["routes"])
		{
			if (!route.contains("destinations"))
				continue;
			bool dest_match = false;
			for (const auto& dest : route["destinations"])
			{
				if (dest == destination)
				{
					dest_match = true;
					break;
				}
			}
			if (!dest_match)
			{
				if (returnValid["FL_CAP"].empty())
				{
					returnValid["FL_CAP"] = "Passed FL cap";
				}
				continue;
			}

			// Part of this monstrosity is due to the fact that there is a "VIA" and "NOT VIA" condition for some routes.
			// This means that if the route goes via a certain waypoint it has a different max height than if it doesn't go via
			// that waypoint.. Eurocontrol really is fun with these things :(

			std::string condition = route.value("condition", "");
			bool condition_matched = false;

			if (condition.empty())
			{
				condition_matched = true;
			}
			else if (condition.rfind("VIA ", 0) == 0 && condition.rfind("NOT VIA", 0) != 0)
			{
				std::string via_str = condition.substr(4);
				auto via_points = split(via_str, ',');
				for (const auto& p : via_points)
				{
					if (std::find(route_tokens.begin(), route_tokens.end(), p) != route_tokens.end())
					{
						condition_matched = true;
						break;
					}
				}

				if (!condition_matched)
				{
					// Attempt fallback to NOT VIA
					for (const auto& fallback : restriction["routes"])
					{
						std::string fb_cond = fallback.value("condition", "");
						if (fb_cond.rfind("NOT VIA ", 0) == 0)
						{
							std::string not_via_str = fb_cond.substr(8);
							auto not_via_points = split(not_via_str, ',');
							bool found = false;
							for (const auto& np : not_via_points)
							{
								if (std::find(route_tokens.begin(), route_tokens.end(), np) != route_tokens.end())
								{
									found = true;
									break;
								}
							}
							if (!found)
							{
								if (fallback.contains("fl_capping"))
								{
									int cap = fallback["fl_capping"];
									min_capping = !min_capping.has_value() ? cap : std::min(min_capping.value(), cap);
								}
								else
								{
									// No cap, so it's all fine in this case
									returnValid["FL_CAP"] = "Passed FL cap";
								}
							}
						}
					}
					continue;
				}
			}
			else if (condition.rfind("NOT VIA ", 0) == 0)
			{
				std::string not_via_str = condition.substr(8);
				auto not_via_points = split(not_via_str, ',');
				bool found = false;
				for (const auto& p : not_via_points)
				{
					if (std::find(route_tokens.begin(), route_tokens.end(), p) != route_tokens.end())
					{
						found = true;
						break;
					}
				}
				if (!found)
					condition_matched = true;
			}
			if (condition_matched && route.contains("fl_capping"))
			{
				int cap = route["fl_capping"];
				min_capping = !min_capping.has_value() ? cap : std::min(min_capping.value(), cap);
			}
			else if (condition_matched && !route.contains("fl_capping"))
			{
				returnValid["FL_CAP"] = "Passed FL cap";
			}
		}
	}
	// After collecting all capping values
	if (min_capping.has_value())
	{
		if (requestedFlightLevel >= min_capping.value())
		{
			returnValid["FL_CAP"] = "Failed FL cap (above " + std::to_string(min_capping.value()) + ")";
			ctx.fail(ValidationCheck::LEVEL_ERROR);
		}
		else
		{
			returnValid["FL_CAP"] = "Passed FL cap (below " + std::to_string(min_capping.value()) + ")";
		}
	}
}

// Method is called when the function (tag) is present
void VFPCPlugin::OnFunctionCall(int FunctionId, const char* ItemString, POINT Pt, RECT Area)
{
	CFlightPlan fp = FlightPlanSelectASEL();

	if (FunctionId == TAG_FUNC_CHECKFP_MENU)
	{
		OpenPopupList(Area, "Check FP", 1);
		AddPopupListElement("Show Checks", "", TAG_FUNC_CHECKFP_CHECK, false, 2, false);

		if (find(AircraftIgnore.begin(), AircraftIgnore.end(), fp.GetCallsign()) != AircraftIgnore.end())
			AddPopupListElement("Enable", "", TAG_FUNC_ON_OFF, false, 2, false);
		else
			AddPopupListElement("Disable", "", TAG_FUNC_ON_OFF, false, 2, false);
	}
	if (FunctionId == TAG_FUNC_CHECKFP_CHECK)
	{
		checkFPDetail();
	}
	if (FunctionId == TAG_FUNC_ON_OFF)
	{
		if (find(AircraftIgnore.begin(), AircraftIgnore.end(), fp.GetCallsign()) != AircraftIgnore.end())
			AircraftIgnore.erase(remove(AircraftIgnore.begin(), AircraftIgnore.end(), fp.GetCallsign()), AircraftIgnore.end());
		else
			AircraftIgnore.emplace_back(fp.GetCallsign());
	}
}

// Get flight plan, and therefore get the first waypoint of the flight plan (ie. SID). Check if the (RFL/1000) corresponds to the SID Min FL and report output "OK" or "FPL"
void VFPCPlugin::OnGetTagItem(CFlightPlan FlightPlan, CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize)
{
	*pColorCode = TAG_COLOR_RGB_DEFINED;

	ValidationContext ctx;

	if (ItemCode == TAG_ITEM_FPCHECK)
	{
		if (strcmp(FlightPlan.GetFlightPlanData().GetPlanType(), "V") > -1)
		{
			*pRGB = TAG_GREEN;
			strcpy_s(sItemString, 16, "VFR");
		}
		else
		{
			map<string, string> messageBuffer;

			validateSid(FlightPlan, ctx, messageBuffer);
			searchRestrictions(FlightPlan, ctx, messageBuffer);

			if (find(AircraftIgnore.begin(), AircraftIgnore.end(), FlightPlan.GetCallsign()) != AircraftIgnore.end())
			{
				*pRGB = TAG_GREY;
				strcpy_s(sItemString, 16, "-");
			}
			else if (ctx.isValid())
			{
				*pRGB = TAG_GREEN;
				strcpy_s(sItemString, 16, "OK!");
			}
			else
			{
				string code;
				code = getFails(messageBuffer, ctx);

				*pRGB = TAG_RED;
				strcpy_s(sItemString, 16, code.c_str());
			}
		}
	}
	else if ((ItemCode == TAG_ITEM_FPCHECK_IF_FAILED || ItemCode == TAG_ITEM_FPCHECK_IF_FAILED_STATIC) && FlightPlan.GetFlightPlanData().GetPlanType() != "V")
	{
		map<string, string> messageBuffer;

		validateSid(FlightPlan, ctx, messageBuffer);
		searchRestrictions(FlightPlan, ctx, messageBuffer);

		if (find(AircraftIgnore.begin(), AircraftIgnore.end(), FlightPlan.GetCallsign()) == AircraftIgnore.end() &&
			messageBuffer["STATUS"] != "Passed")
		{
			*pRGB = TAG_RED;

			if (ItemCode == TAG_ITEM_FPCHECK_IF_FAILED)
			{
				string code;
				code = getFails(messageBuffer, ctx);
				strcpy_s(sItemString, 16, code.c_str());
			}
			else
				strcpy_s(sItemString, 16, "E");
		}
	}
}

// Removes aircraft when they disconnect
void VFPCPlugin::OnFlightPlanDisconnect(CFlightPlan FlightPlan)
{
	AircraftIgnore.erase(remove(AircraftIgnore.begin(), AircraftIgnore.end(), FlightPlan.GetCallsign()), AircraftIgnore.end());
}

// Compiled commands, to be used in the command line window
bool VFPCPlugin::OnCompileCommand(const char* sCommandLine)
{
	if (startsWith(".vfpc reload", sCommandLine))
	{
		sendMessage("Unloading all loaded SIDs...");
		initialSidLoad = false;
		return true;
	}
	if (startsWith(".vfpc debug", sCommandLine))
	{
		if (debugMode)
		{
			debugMessage("Deactivating Debug Mode!");
			debugMode = false;
		}
		else
		{
			debugMode = true;
			debugMessage("Activating Debug Mode!");
		}
		return true;
	}
	if (startsWith(".vfpc load", sCommandLine))
	{
		locale loc;
		string buffer{ sCommandLine };
		buffer.erase(0, 11);
		getSidData();
		return true;
	}
	if (startsWith(".vfpc check", sCommandLine))
	{
		checkFPDetail();
		return true;
	}
	return false;
}

// Sends to you, which checks were failed and which were passed on the selected aircraft
void VFPCPlugin::checkFPDetail()
{
	ValidationContext ctx;
	map<string, string> messageBuffer;

	validateSid(FlightPlanSelectASEL(), ctx, messageBuffer);
	searchRestrictions(FlightPlanSelectASEL(), ctx, messageBuffer);

	string buffer{};
	if (!ctx.isValid())
	{
		messageBuffer["STATUS"] = "Failed";
	}
	if (messageBuffer.find("SEARCH") == messageBuffer.end())
	{
		buffer += messageBuffer["STATUS"] + " SID " + messageBuffer["SID"] + ": ";

		int iterator_count = 1;
		for (auto const& [key, val] : messageBuffer)
		{
			if (key == "CS" || key == "STATUS" || key == "SID" || key.rfind("DEBUG", 0) == 0)
				continue;
			buffer += val + ", ";
		}
		// removes trailing comma
		buffer = buffer.substr(0, buffer.size() - 2);
	}
	else
	{
		buffer = messageBuffer["STATUS"] + ": " + messageBuffer["SEARCH"];
	}

	sendMessage(messageBuffer["CS"], buffer);

	debugMessage("Checking FP: " + messageBuffer["CS"] + ", " + messageBuffer["STATUS"] + ", " + messageBuffer["SEARCH"]);
	auto fails = ctx.failureMessages();

	// Concatenate all fail messages into a single string, then send as one debug message
	if (!fails.empty())
	{
		std::string failMsg = "Failed on: ";
		for (size_t i = 0; i < fails.size(); ++i)
		{
			failMsg += fails[i];
			if (i != fails.size() - 1)
				failMsg += ", ";
		}
		debugMessage("Failures: " + std::to_string(fails.size()) + ". Failures: " + failMsg);
	}
}

string VFPCPlugin::getFails(map<string, string> messageBuffer, ValidationContext& ctx)
{
	vector<string> fails;

	auto messages = ctx.failureMessages();
	for (const auto& message : messages)
	{
		fails.push_back(message);
	}

	std::size_t failures = fails.empty() ? 0 : disCount % fails.size();
	return fails[failures];
}

void VFPCPlugin::OnTimer(int Counter)
{
	blink = !blink;

	if (blink)
	{
		if (disCount < 3)
		{
			disCount++;
		}
		else
		{
			disCount = 0;
		}
	}

	// Loading proper Sids, when logged in
	if (GetConnectionType() != CONNECTION_TYPE_NO && !initialSidLoad)
	{
		string callsign{ ControllerMyself().GetCallsign() };
		sendMessage("Loading all SIDs...");
		getSidData();
		initialSidLoad = true;
	}
	else if (GetConnectionType() == CONNECTION_TYPE_NO && initialSidLoad)
	{
		initialSidLoad = false;
		sendMessage("Unloading", "All loaded SIDs");
	}
}
