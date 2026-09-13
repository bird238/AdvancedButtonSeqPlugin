//***********************************************************************************************
//Impromptu Modular: Modules for VCV Rack by Marc Boulé
//
//See ./LICENSE.md for all licenses
//***********************************************************************************************

#pragma once

#include "rack.hpp"
#include "PanelTheme.hpp"

using namespace rack;


struct OrangeLightIM : GrayModuleLightWidget {
	OrangeLightIM() {
		addBaseColor(SCHEME_ORANGE);
	}
};

struct GreenLightIM : GrayModuleLightWidget {
	GreenLightIM() {
		addBaseColor(SCHEME_GREEN_IM);
	}
};

struct RedLightIM : GrayModuleLightWidget {
	RedLightIM() {
		addBaseColor(SCHEME_RED_IM);
	}
};
