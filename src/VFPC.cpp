#include "stdafx.h"
#include "EuroScopePlugIn.h"
#include "analyzeFP.hpp"

VFPCPlugin* gpMyPlugin = NULL;

void    __declspec (dllexport)    EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
	*ppPlugInInstance = gpMyPlugin = new VFPCPlugin();
}

void    __declspec (dllexport)    EuroScopePlugInExit(void)
{
	delete gpMyPlugin;
}
