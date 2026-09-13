//***********************************************************************************************
//Impromptu Modular: Modules for VCV Rack by Marc Boulé
//
//See ./LICENSE.md for all licenses
//***********************************************************************************************

#pragma once

#include "rack.hpp"

using namespace rack;


static constexpr int panelThemeDefaultValue = 0x2;
static constexpr float panelContrastDefaultValue = 220.0f;
extern NVGcolor SCHEME_RED_IM;
extern NVGcolor SCHEME_GREEN_IM;


void readThemeAndContrastFromDefault();
void loadThemeAndContrastFromDefault(int* panelTheme, float* panelContrast);
