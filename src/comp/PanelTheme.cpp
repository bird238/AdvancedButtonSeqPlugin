//***********************************************************************************************
//Impromptu Modular: Modules for VCV Rack by Marc Boulé
//
//See ./LICENSE.md for all licenses
//***********************************************************************************************

#include "PanelTheme.hpp"

NVGcolor SCHEME_RED_IM = SCHEME_RED;
NVGcolor SCHEME_GREEN_IM = SCHEME_GREEN;

static float defaultPanelContrast;

// Persists SCHEME_RED_IM/SCHEME_GREEN_IM and the default contrast to a settings file shared
// across all Impromptu-derived plugins the user has installed (asset::user, not asset::plugin).
static void writeThemeAndContrastAsDefault() {
	json_t *settingsJ = json_object();
	json_object_set_new(settingsJ, "contrastDefault", json_real(defaultPanelContrast));

	json_t *redImJ = json_array();
	for (int c = 0; c < 3; c++)
		json_array_insert_new(redImJ, c, json_integer(std::round(SCHEME_RED_IM.rgba[c] * 255.0f)));
	json_object_set_new(settingsJ, "redLED_RGB", redImJ);

	json_t *greenImJ = json_array();
	for (int c = 0; c < 3; c++)
		json_array_insert_new(greenImJ, c, json_integer(std::round(SCHEME_GREEN_IM.rgba[c] * 255.0f)));
	json_object_set_new(settingsJ, "greenLED_RGB", greenImJ);

	std::string settingsFilename = asset::user("ImpromptuModular.json");
	FILE *file = fopen(settingsFilename.c_str(), "w");
	if (file) {
		json_dumpf(settingsJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		fclose(file);
	}
	json_decref(settingsJ);
}


void loadThemeAndContrastFromDefault(int* panelTheme, float* panelContrast) {
	*panelTheme = panelThemeDefaultValue;
	*panelContrast = defaultPanelContrast;
}


void readThemeAndContrastFromDefault() {
	std::string settingsFilename = asset::user("ImpromptuModular.json");
	FILE *file = fopen(settingsFilename.c_str(), "r");
	if (!file) {
		defaultPanelContrast = panelContrastDefaultValue;
		writeThemeAndContrastAsDefault();
		return;
	}
	json_error_t error;
	json_t *settingsJ = json_loadf(file, 0, &error);
	if (!settingsJ) {
		fclose(file);
		defaultPanelContrast = panelContrastDefaultValue;
		writeThemeAndContrastAsDefault();
		return;
	}

	json_t *contrastDefaultJ = json_object_get(settingsJ, "contrastDefault");
	defaultPanelContrast = contrastDefaultJ ? json_number_value(contrastDefaultJ) : panelContrastDefaultValue;

	json_t *redImJ = json_object_get(settingsJ, "redLED_RGB");
	if (redImJ) {
		for (int c = 0; c < 3; c++) {
			json_t *redImArrayJ = json_array_get(redImJ, c);
			if (redImArrayJ)
				SCHEME_RED_IM.rgba[c] = ((float) json_integer_value(redImArrayJ)) / 255.0f;
		}
	}

	json_t *greenImJ = json_object_get(settingsJ, "greenLED_RGB");
	if (greenImJ) {
		for (int c = 0; c < 3; c++) {
			json_t *greenImArrayJ = json_array_get(greenImJ, c);
			if (greenImArrayJ)
				SCHEME_GREEN_IM.rgba[c] = ((float) json_integer_value(greenImArrayJ)) / 255.0f;
		}
	}

	fclose(file);
	json_decref(settingsJ);
}
