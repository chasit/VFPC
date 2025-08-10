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
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include <regex>

#define MY_PLUGIN_NAME      "VFPC"
#define MY_PLUGIN_VERSION   "4.1.0"
#define MY_PLUGIN_DEVELOPER "Chasit"
#define MY_PLUGIN_COPYRIGHT "GPL v3"
#define MY_PLUGIN_VIEW_AVISO  "VATSIM Flight Plan Checker"

#define PLUGIN_WELCOME_MESSAGE	"Flight Plan Checker"

using namespace std;
using namespace boost;
using namespace rapidjson;
using namespace EuroScopePlugIn;

class VFPCPlugin :
	public EuroScopePlugIn::CPlugIn
{
public:
	VFPCPlugin();
	virtual ~VFPCPlugin();

	virtual void getSids();

	virtual map<string, string> checkRestrictions(const string& origin, const string& destination, int RFL, const vector<string>& route);

	virtual map<string, string> validizeSid(CFlightPlan flightPlan);

	virtual void OnFunctionCall(int FunctionId, const char* ItemString, POINT Pt, RECT Area);

	//Define OnGetTagItem function
	virtual void OnGetTagItem(CFlightPlan FlightPlan,
		CRadarTarget RadarTarget,
		int ItemCode,
		int TagData,
		char sItemString[16],
		int* pColorCode,
		COLORREF* pRGB,
		double* pFontSize);

	std::vector<std::string> split(const std::string& s, char delimiter)
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

	string destArrayContains(const Value& a, string s) {
		for (SizeType i = 0; i < a.Size(); i++) {
			string test = a[i].GetString();
			SizeType x = static_cast<rapidjson::SizeType>(s.rfind(test, 0));
			if (s.rfind(a[i].GetString(), 0) != -1)
				return a[i].GetString();
		}
		return "";
	}

	bool arrayContains(const Value& a, string s) {
		for (SizeType i = 0; i < a.Size(); i++) {
			if (a[i].GetString() == s)
				return true;
		}
		return false;
	}

	bool arrayContains(const Value& a, char s) {
		for (SizeType i = 0; i < a.Size(); i++) {
			if (a[i].GetString()[0] == s)
				return true;
		}
		return false;
	}

	string arrayToString(const Value& a, char delimiter) {
		string s;
		for (SizeType i = 0; i < a.Size(); i++) {
			s += a[i].GetString()[0];
			if (i != a.Size() - 1)
				s += delimiter;
		}
		return s;
	}
	bool routeContains(string s, const Value& a) {
		for (SizeType i = 0; i < a.Size(); i++) {
			bool dd = contains(s, a[i].GetString());
			if (contains(s, a[i].GetString()))
				return true;
		}
		return false;
	}

	virtual void OnFlightPlanDisconnect(CFlightPlan FlightPlan);

	virtual bool OnCompileCommand(const char* sCommandLine);

	virtual void debugMessage(string message);

	virtual void sendMessage(string type, string message);

	virtual void sendMessage(string message);

	virtual void checkFPDetail();

	virtual string getFails(map<string, string> messageBuffer);

	virtual void OnTimer(int Count);

	virtual bool routeContainsAirways(CFlightPlan flightPlan, const Value& a);

	virtual void logToFile(const std::string& message);


protected:
	Document config;
	Value sid_details;
	Value sid_mapping;
	Value restrictions;
	map<string, rapidjson::SizeType> airports;
};

