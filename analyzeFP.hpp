#pragma once
#include "EuroScopePlugIn.h"
#include <sstream>
#include <iostream>
#include <string>
#include "Constant.hpp"
#include <fstream>
#include <vector>
#include <map>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <regex>
#include <fstream>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <variant>
#include "validationContext.hpp"
#include <cstdio>

#define MY_PLUGIN_NAME "VFPC V2"
#define MY_PLUGIN_VERSION "4.0.0"
#define MY_PLUGIN_DEVELOPER "Chasit"
#define MY_PLUGIN_COPYRIGHT "GPL v3"
#define MY_PLUGIN_VIEW_AVISO "VATSIM Flight Plan Checker"

using namespace std;
using namespace boost;
using json = nlohmann::json;
using namespace EuroScopePlugIn;

class VFPCPlugin : public EuroScopePlugIn::CPlugIn
{
public:
	VFPCPlugin();
	virtual ~VFPCPlugin();

	virtual void getSidData();

	virtual void validate_sid(CFlightPlan flightPlan, ValidationContext &ctx, map<string, string>& returnValid);

	virtual void search_restrictions(
		CFlightPlan flightPlan, ValidationContext &ctx, map<string, string>& returnValid);

	virtual void OnFunctionCall(int FunctionId, const char *ItemString, POINT Pt, RECT Area);

	virtual void OnGetTagItem(CFlightPlan FlightPlan,
							  CRadarTarget RadarTarget,
							  int ItemCode,
							  int TagData,
							  char sItemString[16],
							  int *pColorCode,
							  COLORREF *pRGB,
							  double *pFontSize);

	std::vector<std::string> split(const std::string &s, char delimiter)
	{
		std::vector<std::string> tokens;
		std::string token;
		std::istringstream tokenStream(s);
		while (std::getline(tokenStream, token, delimiter))
		{
			token.erase(remove_if(token.begin(), token.end(), ::isspace), token.end());
			if (!token.empty())
				tokens.push_back(token);
		}
		return tokens;
	}

	virtual void logToFile(const std::string &message);

	virtual void OnFlightPlanDisconnect(CFlightPlan FlightPlan);

	virtual bool OnCompileCommand(const char *sCommandLine);

	virtual void debugMessage(string message);

	virtual void sendMessage(string type, string message);

	virtual void sendMessage(string message);

	virtual void checkFPDetail();

	virtual string getFails(map<string, string> messageBuffer, ValidationContext &ctx);

	virtual void OnTimer(int Count);

protected:
	vector<string> airports;
};
