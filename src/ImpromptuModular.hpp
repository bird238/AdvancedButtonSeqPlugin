//***********************************************************************************************
//Shared engine/GUI utilities used by AdvancedButtonSeq.
//Originates from Marc Boulé's Impromptu Modular; trimmed to only what this module needs.
//See ./LICENSE.md for all licenses
//***********************************************************************************************

#pragma once

#include "rack.hpp"
#include "comp/Components.hpp"

using namespace rack;


extern Plugin *pluginInstance;
extern Model *modelAdvancedButtonSeq;


enum RetrigGatesOnResetId {RGOR_NONE, RGOR_YES, RGOR_NRUN};
static constexpr float clockIgnoreOnResetDuration = 0.001f;// mute clock for 1ms after power-up/reset so the first step plays cleanly


struct VecPx : Vec {
	// px-space Vec, needed because PanelLabelsWidget is sized directly from the SVG panel's pixel box
	static constexpr float scl = 5.08f / 15.0f;
	VecPx(float _x, float _y) {
		x = mm2px(_x * scl);
		y = mm2px(_y * scl);
	}
};


struct RefreshCounter {
	// UI-rate work (displays, menus) doesn't need to run every audio sample; input polling still
	// needs to stay above ~1kHz so 1ms triggers aren't missed.
	static const unsigned int displayRefreshStepSkips = 256;
	static const unsigned int userInputsStepSkipMask = 0xF;

	unsigned int refreshCounter = (random::u32() % displayRefreshStepSkips);// staggered so many instances don't all refresh on the same sample

	bool processInputs() {
		return ((refreshCounter & userInputsStepSkipMask) == 0);
	}
	bool processLights() {// must be called unconditionally: this also advances the counter
		refreshCounter++;
		bool process = refreshCounter >= displayRefreshStepSkips;
		if (process) {
			refreshCounter = 0;
		}
		return process;
	}
};


struct Trigger {
	bool state = true;

	void reset() {
		state = true;
	}

	bool isHigh() {
		return state;
	}

	bool process(float in) {
		if (state) {
			if (in <= 0.1f) {
				state = false;
			}
		}
		else {
			if (in >= 1.0f) {
				state = true;
				return true;
			}
		}
		return false;
	}
};


inline void calcNoteAndOct(float cv, int* note12, int* oct0) {
	// note12: 0-11 for C..B; oct0: 0 = octave 4 (C4 etc.)
	eucDivMod((int)std::round(cv * 12.0f), 12, oct0, note12);
}
int printNote(float cvVal, char* text, bool sharp);
