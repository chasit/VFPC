#include "stdafx.h"
#include "analyzeFP.hpp"
#include <optional>
#include <chrono>

extern "C" IMAGE_DOS_HEADER __ImageBase;

bool blink;
bool debugMode, initialSidLoad;

int disCount;

const int checksAmount = 6;

ifstream sidDatei;
char DllPathFile[_MAX_PATH];
string pfad;

vector<string> sidName;
vector<string> sidEven;
vector<int> sidMin;
vector<int> sidMax;
vector<string> AircraftIgnore;

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

	// Get Path of the Sid.txt
	GetModuleFileNameA(HINSTANCE(&__ImageBase), DllPathFile, sizeof(DllPathFile));
	pfad = DllPathFile;
	pfad.resize(pfad.size() - strlen("VFPC.dll"));
	pfad += "Sids.json";

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

/*
	Custom Functions
*/

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
}

void VFPCPlugin::sendMessage(string message)
{
	DisplayUserMessage("Message", "VFPC", message.c_str(), true, true, true, false, false);
}

// Parses the Sid.json file
void VFPCPlugin::getSids()
{
	stringstream ss;
	ifstream ifs;
	ifs.open(pfad.c_str(), ios::binary);
	ss << ifs.rdbuf();
	ifs.close();

	if (config.Parse<0>(ss.str().c_str()).HasParseError())
	{
		string msg = str(boost::format("An error parsing VFPC configuration occurred. Error: %s (Offset: %i)\nOnce fixed, reload the config by typing '.vfpc reload'") % config.GetParseError() % config.GetErrorOffset());
		sendMessage(msg);

		config.Parse<0>("[{\"icao\": \"XXXX\"}]");
	}

	if (config.HasMember("sid_mapping"))
		sid_mapping = config["sid_mapping"];
	if (config.HasMember("sid_details"))
		sid_details = config["sid_details"];
	if (config.HasMember("restrictions"))
		restrictions = config["restrictions"];

	airports.clear();
	debugMessage("Sid details size: " + to_string(sid_details.Size()));
	for (SizeType i = 0; i < sid_details.Size(); i++)
	{
		string t = sid_details[i]["icao"].GetString();
		debugMessage("airport: " + t);
		const Value& airport = sid_details[i];
		if (airport.HasMember("icao") && airport["icao"].IsString())
		{
			string airport_icao = airport["icao"].GetString();
			airports.insert(pair<string, SizeType>(airport_icao, i));
		}
	}
}

map<string, string> VFPCPlugin::checkRestrictions(const string& origin, const string& destination, int RFL, const vector<string>& route_tokens)
{
	map<string, string> returnItems;

	bool has_failed = false;

	if (!restrictions.IsArray())
	{
		returnItems["STATUS"] = "Passed!";
	}

	std::optional<int> min_capping;

	for (SizeType i = 0; i < restrictions.Size(); i++)
	{
		const Value& restriction = restrictions[i];

		if (!restriction.HasMember("from") || !restriction["from"].IsArray())
			continue;

		// Check departure match
		bool originMatch = false;
		for (SizeType j = 0; j < restriction["from"].Size(); j++)
		{
			if (origin == restriction["from"][j].GetString())
			{
				originMatch = true;
				break;
			}
		}

		// Check forbidden_fls, this is the "ALL" restriction, regardless if origin matches
		if (restriction.HasMember("id") && std::string(restriction["id"].GetString()) == "ALL")
		{
			if (restriction.HasMember("forbidden_fls") && restriction["forbidden_fls"].IsInt())
			{
				int forbidden = restriction["forbidden_fls"].GetInt();
				if ((RFL / 100) == forbidden)
				{
					returnItems["FORBIDDEN_FL"] = "Failed: Forbidden FL " + to_string(forbidden);
					has_failed = true;
				}
			}
		}


		if (!originMatch)
			continue;

		// No routes = nothing else to check
		if (!restriction.HasMember("routes") || !restriction["routes"].IsArray())
			continue;

		for (SizeType r = 0; r < restriction["routes"].Size(); r++)
		{
			const Value& routeRule = restriction["routes"][r];

			// Destination match
			if (!routeRule.HasMember("destinations") || !routeRule["destinations"].IsArray())
				continue;
			bool destMatch = false;
			for (SizeType d = 0; d < routeRule["destinations"].Size(); d++)
			{
				string pattern = routeRule["destinations"][d].GetString();
				if (pattern.find('*') != string::npos)
				{
					string base = pattern.substr(0, pattern.find('*'));
					if (destination.rfind(base, 0) == 0)
						destMatch = true;
					break;
				}
				else if (destination == pattern)
				{
					std::cout << "Found something: " << pattern << ". Restr: " << restriction["id"].GetString();
					destMatch = true;
					break;
				}
			}
			if (!destMatch) {
				if (returnItems["FL_CAP"].empty())
				{
					returnItems["FL_CAP"] = "Passed FL cap";
				}
				continue;
			}

			// Condition check
			string condition = routeRule.HasMember("condition") && routeRule["condition"].IsString()
				? routeRule["condition"].GetString()
				: "";
			bool condition_matched = false;

			if (condition.empty())
			{
				condition_matched = true;
			}
			else if (condition.rfind("VIA ", 0) == 0 && condition.rfind("NOT VIA", 0) != 0)
			{
				string via_str = condition.substr(4);
				auto via_points = split(via_str, ',');
				for (auto& p : via_points)
				{
					boost::trim(p);
					if (std::find(route_tokens.begin(), route_tokens.end(), p) != route_tokens.end())
					{
						condition_matched = true;
						break;
					}
				}

				if (!condition_matched)
				{
					// Fallback to NOT VIA rules inside same restriction
					for (SizeType fb = 0; fb < restriction["routes"].Size(); fb++)
					{
						const Value& fallback = restriction["routes"][fb];
						if (!fallback.HasMember("condition") || !fallback["condition"].IsString())
							continue;
						string fb_cond = fallback["condition"].GetString();

						if (fb_cond.rfind("NOT VIA ", 0) == 0)
						{
							string not_via_str = fb_cond.substr(8);
							auto not_via_points = split(not_via_str, ',');
							bool found = false;
							for (auto& np : not_via_points)
							{
								boost::trim(np);
								if (std::find(route_tokens.begin(), route_tokens.end(), np) != route_tokens.end())
								{
									found = true;
									break;
								}
							}
							if (!found)
							{
								if (fallback.HasMember("fl_capping") && fallback["fl_capping"].IsInt())
								{
									int cap = fallback["fl_capping"].GetInt();
									if (!min_capping.has_value() || cap < min_capping.value())
									{
										min_capping = cap;
									}
									else
									{
										returnItems["FL_CAP"] = "Passed FL cap";
									}
								}
								else
								{
									returnItems["FL_CAP"] = "Passed FL cap";
								}
							}
						}
					}
					continue; // Skip rest of VIA rule
				}
			}
			else if (condition.rfind("NOT VIA ", 0) == 0)
			{
				string not_via_str = condition.substr(8);
				auto not_via_points = split(not_via_str, ',');
				bool found = false;
				for (auto& p : not_via_points)
				{
					boost::trim(p);
					if (std::find(route_tokens.begin(), route_tokens.end(), p) != route_tokens.end())
					{
						found = true;
						break;
					}
				}
				if (!found)
					condition_matched = true;
			}

			// Apply FL capping if matched
			if (condition_matched && routeRule.HasMember("fl_capping") && routeRule["fl_capping"].IsInt())
			{
				int cap = routeRule["fl_capping"].GetInt();
				if (!min_capping.has_value() || cap < min_capping.value())
				{
					min_capping = cap;
				}
			}
			else if (condition_matched && !routeRule.HasMember("fl_capping"))
			{
				returnItems["FL_CAP"] = "Passed FL cap";
			}
		}
	}
	// Final FL check
	if (min_capping.has_value()) {
		if ((RFL / 100) > min_capping.value()) {
			returnItems["FL_CAP"] = "Failed: FL above cap (" + to_string(min_capping.value()) + ")";
			has_failed = true;
		}
		else {
			returnItems["FL_CAP"] = "Passed FL cap (below " + std::to_string(min_capping.value()) + ")";
		}
	}

	if (!has_failed)
	{
		returnItems["STATUS"] = "Passed!";
		returnItems["RESTRICTIONS"] = "No route restrictions found";
	}
	else
	{
		returnItems["STATUS"] = "Failed!";
	}
	return returnItems;
}

// Checks if the flight plan is valid according to rules specified.
map<string, string> VFPCPlugin::validizeSid(CFlightPlan flightPlan)
{
	map<string, string> returnValid;

	string callsign = flightPlan.GetCallsign();
	debugMessage("Now starting with checking: " + callsign);

	returnValid["CS"] = flightPlan.GetCallsign();
	returnValid["STATUS"] = "Passed";
	bool valid{ false };

	string origin = flightPlan.GetFlightPlanData().GetOrigin();
	boost::to_upper(origin);
	string destination = flightPlan.GetFlightPlanData().GetDestination();
	boost::to_upper(destination);
	SizeType origin_int;
	int RFL = flightPlan.GetFlightPlanData().GetFinalAltitude();
	int requestedFlightLevel = RFL / 1000;

	vector<string> route = split(flightPlan.GetFlightPlanData().GetRoute(), ' ');
	for (std::size_t i = 0; i < route.size(); i++)
	{
		boost::to_upper(route[i]);
	}

	if (strcmp(flightPlan.GetFlightPlanData().GetPlanType(), "V") > -1)
	{
		returnValid["SEARCH"] = "VFR Flight, no SID required!";
		returnValid["STATUS"] = "Passed";
		return returnValid;
	}

	string fullSidName = flightPlan.GetFlightPlanData().GetSidName();
	boost::to_upper(fullSidName);

	// Flightplan has SID
	if (!fullSidName.length()) {
		returnValid["SEARCH"] = "Flightplan doesn't have SID set!";
		returnValid["STATUS"] = "Failed";
		return returnValid;
	}

	if (fullSidName == "DIFT") {
		returnValid["SEARCH"] = "DIFT Set, not checking the flightplan!";
		returnValid["STATUS"] = "Passed";
		return returnValid;
	}

	string sidName = fullSidName.substr(0, fullSidName.find_first_of("0123456789")); // This is VALKO in VALKO3S
	if (sidName.length() != 0)
		boost::to_upper(sidName);
	string sid_suffix; // This is 3S in VALKO3S for example
	if (sidName.length() != fullSidName.length()) {
		sid_suffix = fullSidName.substr(fullSidName.find_first_of("0123456789"), fullSidName.length());
		boost::to_upper(sid_suffix);
	}

	debugMessage("SID Set for callsign: " + returnValid["CS"] + " is: " + fullSidName + ". The sidname is: " + sidName);

	// Did not find a valid SID
	if (sid_suffix.length() == 0 && "VCT" != sidName) {
		returnValid["SID_ERR"] = "Flightplan doesn't have SID set!";
		returnValid["STATUS"] = "Failed";
		return returnValid;
	}

	std::string exit_point = sidName;
	if (sid_mapping.HasMember(sidName.c_str())) {
		exit_point = sid_mapping[sidName.c_str()].GetString();
		boost::to_upper(exit_point);
	}

	// Airport defined
	if (airports.find(origin) == airports.end()) {
		returnValid["SEARCH"] = "No valid Airport found!";
		returnValid["STATUS"] = "Failed";
		return returnValid;
	}
	else
		origin_int = airports[origin];

	// Any SIDs defined
	if (!sid_details[origin_int].HasMember("sids") || sid_details[origin_int]["sids"].IsArray()) {
		returnValid["SID_ERR"] = "No SIDs defined!";
		returnValid["STATUS"] = "Failed";
		return returnValid;
	}

	// Needed SID defined
	if (!sid_details[origin_int]["sids"].HasMember(sidName.c_str()) || !sid_details[origin_int]["sids"][sidName.c_str()].IsArray()) {
		returnValid["SID_ERR"] = "No valid SID found!";
		returnValid["STATUS"] = "Failed";
		return returnValid;
	}

	debugMessage(returnValid["CS"] + " passed the initial checks");

	const Value& conditions = sid_details[origin_int]["sids"][sidName.c_str()];
	for (SizeType i = 0; i < conditions.Size(); i++) {
		returnValid.clear();
		returnValid["SID"] = fullSidName;

		returnValid["CS"] = flightPlan.GetCallsign();
		bool passed[checksAmount]{ false };
		valid = false;


		if (conditions[i].HasMember("direction") && conditions[i]["direction"].IsString()) {
			string direction = conditions[i]["direction"].GetString();
			bool is_even = (requestedFlightLevel % 2 == 0);

			if ((direction == "ODD" && is_even) || (direction == "EVEN" && !is_even))
			{
				returnValid["DIRECTION"] = "Failed " + direction;
			}
			else {
				returnValid["DIRECTION"] = "Passed " + direction;
				passed[0] = true;
			}
		}
		else {
			returnValid["DIRECTION"] = "No direction restraint";
			passed[0] = true;

		}

		debugMessage(returnValid["CS"] + " passed the destination checks");


		if (conditions[i].HasMember("max_fl") && conditions[i]["max_fl"].IsInt()) {
			int max_fl = conditions[i]["max_fl"].GetInt();
			if ((requestedFlightLevel * 10) > max_fl)
			{
				returnValid["MAX_FL"] = "Failed max FL for SID (above " + std::to_string(max_fl) + ")";
			}
			else
			{
				returnValid["MAX_FL"] = "Passed max FL for SID (below " + std::to_string(max_fl) + ")";
				passed[1] = true;

			}
		}
		else {
			returnValid["MAX_FL"] = "No max FL restraint for SID";
			passed[1] = true;
		}

		debugMessage(returnValid["CS"] + " passed the max FL checks");


		if (conditions[i].HasMember("destinations") && conditions[i]["destinations"].IsArray() && !conditions[i]["destinations"].Empty())
		{
			const auto& destinations = conditions[i]["destinations"];

			bool destination_found = destArrayContains(destinations, destination) != "";

			if (!destination_found)
			{
				returnValid["DESTINATION"] = "Failed SID not valid for destination " + destination;
			}
			else
			{
				returnValid["DESTINATION"] = "Passed SID valid for destination " + destination;
				passed[2] = true;
			}

		}
		else
		{
			returnValid["DESTINATION"] = "No destination restraint";
			passed[2] = true;
		}

		debugMessage(returnValid["CS"] + " passed the destinations checks");

		if (std::find(route.begin(), route.end(), exit_point) != route.end()) {
			returnValid["ROUTE"] = "Exit point '" + exit_point + "' found in route";
			passed[3] = true;
		}
		else {
			returnValid["ROUTE"] = "Exit point '" + exit_point + "' NOT found in route";
		}

		debugMessage(returnValid["CS"] + " passed the EXIT point check");


		// Check airway requirement: only perform check if airway_required does not exist or is not set to false
		// The airway check should start after the mapped exit point (sid_mapping), not always at route[1]
		if ((!conditions[i].HasMember("airway_required") || conditions[i]["airway_required"].GetBool() != false) && route.size() > 1)
		{

			// Find the index of the exit point in the route tokens
			int exit_idx = -1;
			for (size_t i = 0; i < route.size(); ++i) {
				if (route[i] == exit_point) {
					exit_idx = static_cast<int>(i);
					break;
				}
			}

			debugMessage("Exit IDX is found at: " + to_string(exit_idx) + " for SID: " + sidName + ". SID potentially has different exit point which is: " + exit_point);

			// If not found, fallback to route[1] as before
			// Determine where to start checking after SID exit
			int start_idx = (exit_idx != -1 && exit_idx + 1 < static_cast<int>(route.size())) ? exit_idx + 1 : 0;

			bool valid = true;
			bool last_was_airway = false;

			for (int i = start_idx; i < static_cast<int>(route.size()); ++i) {
				const string& token = route[i];

				// Determine if token is an airway
				bool is_airway = std::any_of(token.begin(), token.end(), ::isalpha) &&
					std::any_of(token.begin(), token.end(), ::isdigit);

				// Determine if token is a navfix (Waypoint/VOR/NDB)
				bool is_navfix = token.length() >= 2 &&
					token.length() <= 5 &&
					std::all_of(token.begin(), token.end(), ::isalnum) &&
					std::all_of(token.begin(), token.end(), ::isupper);

				// Determine if token is a STAR (arrival) — for now, treat all navfixes ending with digit as STAR
				// Essentially we check if the second to last token is a digit
				bool is_star = is_navfix && std::isdigit(token.back());

				// First token after SID exit
				if (i == start_idx) {
					if (!(is_airway || is_star)) {
						valid = false;
						returnValid["AIRWAYS"] = "Failed: first post-SID token must be an airway or STAR, found '" + token + "'";
						returnValid["STATUS"] = "Failed";
						break;
					}
					last_was_airway = is_airway;
					continue;
				}
			}

			if (valid) {
				returnValid["AIRWAYS"] = "Passed airway requirement";
				passed[4] = true;
			}
		}
		else
		{
			returnValid["AIRWAYS"] = "Passed airway requirement";
			passed[4] = true;
		}

		debugMessage(returnValid["CS"] + " passed the airways checks");


		// Check restrictions, we require a bit more route information here.
		// This lists all the points part of the FP, even the ones routing over airways!

		int total_points = flightPlan.GetExtractedRoute().GetPointsNumber();

		vector<string> route_tokens;
		for (int i = 0; i < total_points; i++)
		{
			string item = flightPlan.GetExtractedRoute().GetPointName(i);
			route_tokens.push_back(item);
		}


		map<string, string> restrResults = checkRestrictions(origin, destination, RFL, route_tokens);

		debugMessage(returnValid["CS"] + " passed the restrictions checks");

		returnValid.insert(restrResults.begin(), restrResults.end());

		if (restrResults["STATUS"].rfind("Passed", 0) == 0) {
			passed[5] = true;
		}

		bool passedVeri{ false };

		for (int i = 0; i < checksAmount; i++) {
			if (passed[i])
			{
				passedVeri = true;
			}
			else {
				passedVeri = false;
				break;
			}
		}
		if (passedVeri) {
			returnValid["STATUS"] = "Passed";
			break;
		}
		else {
			returnValid["STATUS"] = "Failed";
			if (!passed[0])
				continue;
			else
				break;
		}

	}
	debugMessage(callsign + " passed all checks, now responding. . . . ");
	return returnValid;
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

// Get FlightPlan, and therefore get the first waypoint of the flightplan (ie. SID). Check if the (RFL/1000) corresponds to the SID Min FL and report output "OK" or "FPL"
void VFPCPlugin::OnGetTagItem(CFlightPlan FlightPlan, CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize)
{
	*pColorCode = TAG_COLOR_RGB_DEFINED;

	if (ItemCode == TAG_ITEM_FPCHECK)
	{
		if (strcmp(FlightPlan.GetFlightPlanData().GetPlanType(), "V") > -1)
		{
			*pRGB = TAG_GREEN;
			strcpy_s(sItemString, 16, "VFR");
		}
		else
		{
			map<string, string> messageBuffer = validizeSid(FlightPlan);

			if (find(AircraftIgnore.begin(), AircraftIgnore.end(), FlightPlan.GetCallsign()) != AircraftIgnore.end())
			{
				*pRGB = TAG_GREY;
				strcpy_s(sItemString, 16, "-");
			}
			else if (messageBuffer["STATUS"] == "Passed")
			{
				*pRGB = TAG_GREEN;
				strcpy_s(sItemString, 16, "OK!");
			}
			else
			{
				string code;
				code = getFails(messageBuffer);
				*pRGB = TAG_RED;
				strcpy_s(sItemString, 16, code.c_str());
			}
		}
	}
	else if ((ItemCode == TAG_ITEM_FPCHECK_IF_FAILED || ItemCode == TAG_ITEM_FPCHECK_IF_FAILED_STATIC) && FlightPlan.GetFlightPlanData().GetPlanType() != "V")
	{
		map<string, string> messageBuffer = validizeSid(FlightPlan);

		if (find(AircraftIgnore.begin(), AircraftIgnore.end(), FlightPlan.GetCallsign()) == AircraftIgnore.end() &&
			messageBuffer["STATUS"] != "Passed")
		{
			*pRGB = TAG_RED;

			if (ItemCode == TAG_ITEM_FPCHECK_IF_FAILED)
			{
				string code;
				code = getFails(messageBuffer);
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
		sidName.clear();
		sidEven.clear();
		sidMin.clear();
		sidMax.clear();
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
		getSids();
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
	map<string, string> messageBuffer = validizeSid(FlightPlanSelectASEL());

	string route = FlightPlanSelectASEL().GetFlightPlanData().GetRoute();
	debugMessage(route);

	int total_points = FlightPlanSelectASEL().GetExtractedRoute().GetPointsNumber();

	vector<string> route_tokens;
	for (int i = 0; i < total_points; i++)
	{
		string item = FlightPlanSelectASEL().GetExtractedRoute().GetPointName(i);
		route_tokens.push_back(item);
	}

	// combine the route tokens vector into one string and send the message
	string route_string = boost::algorithm::join(route_tokens, ", ");

	// Send the route tokens as a message
	debugMessage("Route Tokens: " + route_string);


	string buffer{};

	debugMessage("Messge buff len: " + to_string(messageBuffer.size()));
	debugMessage("SID IS SET AS: " + messageBuffer["SID"]);

	if (messageBuffer.find("SEARCH") == messageBuffer.end())
	{
		buffer += messageBuffer["STATUS"] + " SID " + messageBuffer["SID"] + ", ";

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
	// debugMessage(
	// 	(messageBuffer["CS"] + " First point (or airway) after the SID: " + messageBuffer["DEBUG_AIRWAY_CHK"] +
	// 	", First waypoint (can differ from first fix): " + messageBuffer["DEBUG_AIRWAY_CHK2"] + ", First fix: " +
	// 	messageBuffer["DEBUG_AIRWAY_CHK3"] + ". Result Regex: " + messageBuffer["DEBUG_AIRWAY_CHK4"])
	// );
}

string VFPCPlugin::getFails(map<string, string> messageBuffer)
{
	vector<string> fails;
	debugMessage("Attempting fails");
	// if (messageBuffer.find("STATUS") != messageBuffer.end()) {
	// 	fails.push_back("SID");
	// }

	if (messageBuffer.find("STATUS") != messageBuffer.end() || messageBuffer["SID_ERR"].find_first_of("Failed") == 0)
	{

		debugMessage("SID ERR");
		fails.push_back("SID");
	}

	if (messageBuffer["ROUTE"].find_first_of("Failed") == 0 || messageBuffer["AIRWAYS"].find_first_of("Failed") == 0)
	{
		debugMessage("RTE");
		fails.push_back("RTE");
	}

	if (messageBuffer["FL"].find_first_of("Failed") == 0 || messageBuffer["MAX_FL"].find_first_of("Failed") == 0 || messageBuffer["MIN_FL"].find_first_of("Failed") == 0 || messageBuffer["FORBIDDEN_FL"].find_first_of("Failed") == 0 || messageBuffer["FL_CAP"].find_first_of("Failed") == 0 || messageBuffer["DIRECTION"].find_first_of("Failed") == 0)
	{
		debugMessage("FL");
		fails.push_back("FL");
	}

	debugMessage("while");
	debugMessage("couldnt: " + to_string(disCount));
	debugMessage("fails.size(): " + to_string(fails.size()));

	std::size_t failures = fails.empty() ? 0 : disCount % fails.size();
	debugMessage("Failures: " + to_string(failures));
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
		getSids();
		initialSidLoad = true;
	}
	else if (GetConnectionType() == CONNECTION_TYPE_NO && initialSidLoad)
	{
		sidName.clear();
		sidEven.clear();
		sidMin.clear();
		sidMax.clear();
		initialSidLoad = false;
		sendMessage("Unloading", "All loaded SIDs");
	}
}

// Checks whether the route contains an airway after the sid
bool VFPCPlugin::routeContainsAirways(CFlightPlan flightPlan, const Value& airways)
{
	bool routeContainsAirway = false;
	// all points of the FP are part of the extracted route, they're numbered.
	// Therefore we first get all the numbers (e.g. 8) and then go through all of them to see
	// if any of the points match any of the given airways, one does, then we return true
	int total_points = flightPlan.GetExtractedRoute().GetPointsNumber();

	for (int i = 0; i < total_points; i++)
	{
		string item = flightPlan.GetExtractedRoute().GetPointName(i);
		// Apparently this is broken in this project for some reason...
		// auto find = std::find(airways.Begin(), airways.End(), item);
		for (SizeType j = 0; j < airways.Size(); j++)
		{
			if (item == airways[j].GetString())
			{
				routeContainsAirway = true;
				return routeContainsAirway;
			}
		}
	}
	return routeContainsAirway;
}
