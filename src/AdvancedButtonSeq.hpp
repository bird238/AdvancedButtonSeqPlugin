//***********************************************************************************************
//Advanced Button Seq: a 6-channel, 128-step graphical sequencer for VCV Rack
//
//A new, independent module built on top of the Impromptu Modular plugin, inspired by the
//architecture and live-performance controls of BigButtonSeq2 (RND / CHAN / LEN / DEL / FILL /
//BIG BUTTON / CLOCK / RESET / MEM / SNAP), extended with a full graphical 16-step-per-page
//editor (128 steps x 6 channels), per-step CV2 / CV3 / Gate-length editing, Tie/Glide, a 6x5
//CV+Gate output matrix, Sample & Hold, Undo/Redo, Copy/Paste, sequence shifting, and native /
//MIDI file export.
//
//See ./LICENSE.md for all licenses
//***********************************************************************************************

#pragma once

#include "ImpromptuModular.hpp"
#include <vector>
#include <cstdint>
#include <utility>


static const int abs_NUM_CHANNELS = 6;
static const int abs_NUM_STEPS = 128;
static const int abs_STEPS_PER_PAGE = 16;
static const int abs_NUM_PAGES = abs_NUM_STEPS / abs_STEPS_PER_PAGE;// 8
static const size_t abs_CVCONT_REC_MAX_SAMPLES = 20000000;// safety cap (~416s @ 48kHz) against a runaway/very slow clock
static const int abs_CVCONT_SCOPE_N = 1024;// rolling real-time scope window (~5.1s @ the 5ms sample spacing below)


struct AdvancedButtonSeq : Module {
	enum ParamIds {
		RND_PARAM,
		CHAN_PARAM,
		LEN_PARAM,
		DEL_PARAM,
		FILL_PARAM,
		BIG_PARAM,
		CLOCK_PARAM,
		RESET_PARAM,
		WRITEFILL_PARAM,// MEM
		QUANTIZEBIG_PARAM,// SNAP
		BANK_PARAM,
		DISPMODE_PARAM,
		NOTEDISP_PARAM,// volts <-> notes format for CV1 displays
		ENUMS(SAMPLEHOLD_PARAM, 3),// CV1, CV2, CV3 rows of the output matrix
		ENUMS(PAGE_PARAM, abs_NUM_PAGES),
		CVCONT_MODE_PARAM,// CV cont cyclic sampler: 0 = off/playback, 1 = record, 2 = pass-through
		SCOPE_SPEED_PARAM,// oscilloscope rolling-window speed; fully clockwise = synced to clock (one loop)
		GATE_LEN_MODE_PARAM,// 0 = GATE_IN is a normal alternate trigger, 1 = GATE_IN's hold time is recorded as gate length
		NUM_PARAMS
	};
	enum InputIds {
		CLK_INPUT,
		RESET_INPUT,
		CHAN_INPUT,
		LEN_INPUT,
		RND_INPUT,
		DEL_INPUT,
		FILL_INPUT,
		BIG_INPUT,
		BANK_INPUT,
		CLEAR_INPUT,
		CV1_INPUT,
		CV2_INPUT,
		CV3_INPUT,
		CVCONT_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(CV1_OUTPUT, abs_NUM_CHANNELS),
		ENUMS(CV2_OUTPUT, abs_NUM_CHANNELS),
		ENUMS(CV3_OUTPUT, abs_NUM_CHANNELS),
		ENUMS(CVCONT_OUTPUT, abs_NUM_CHANNELS),
		ENUMS(GATE_OUTPUT, abs_NUM_CHANNELS),
		EOC_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(CHAN_LIGHT, abs_NUM_CHANNELS),// lit only for the active channel
		BIG_LIGHT,
		BIGC_LIGHT,
		WRITEFILL_LIGHT,
		QUANTIZEBIG_LIGHT,
		ENUMS(BANK_LIGHT, 2),// A / B
		ENUMS(SAMPLEHOLD_LIGHT, 3),
		ENUMS(PAGE_LIGHT, abs_NUM_PAGES),
		EOC_LIGHT,
		NUM_LIGHTS
	};

	// Need to save, no reset
	// Inherited from the shared Impromptu module template; this panel has no dark/light variant,
	// so these are persisted but otherwise unused.
	int panelTheme;
	float panelContrast;

	// Need to save, with reset
	int indexStep;
	int channel;// active channel for editing/recording (0 to 5)
	int page;// active UI page (0 to 7)
	int bank[abs_NUM_CHANNELS];// pattern bank A(0)/B(1) currently active per channel
	float cv1[abs_NUM_CHANNELS][2][abs_NUM_STEPS];
	float cv2[abs_NUM_CHANNELS][2][abs_NUM_STEPS];
	float cv3[abs_NUM_CHANNELS][2][abs_NUM_STEPS];
	float gl[abs_NUM_CHANNELS][2][abs_NUM_STEPS];// 0..1, 1.0 = Tie/Glide
	float cvCont[abs_NUM_CHANNELS][2][abs_NUM_STEPS];
	uint64_t gates[abs_NUM_CHANNELS][2][2];// [channel][bank][128 steps packed as two 64-bit words]
	bool writeFillsToMemory;
	bool quantizeBig;
	int retrigGatesOnReset;
	bool sampleHoldEnabled[3];// CV1, CV2, CV3 output-matrix rows
	bool followPlayhead;// optional: PAGE auto-follows the playing step

	// No need to save, with reset
	long clockIgnoreOnReset;
	double lastPeriod;
	double clockTime;
	int pendingOp;// 0 = nothing pending, +1 = pending big button write, -1 = pending del
	float pendingCV1, pendingCV2, pendingCV3;
	bool fillPressed;
	// Gate-length-record mode: GATE_IN is read as an actual gate, not a static CV -- the step is
	// written at the rising edge, and the length is measured and stored at the falling edge, so the
	// recorded value tracks how long GATE_IN was physically held. gateLenTargetStep tracks whichever
	// step is currently "live" under the hold, advancing via legato if the hold outlasts one step.
	bool gateLenHeld;
	int gateLenTargetChannel;
	int gateLenTargetStep;

	// No need to save, no reset
	RefreshCounter refresh;
	float bigLight = 0.0f;
	int length = 32;
	float sampleHoldBuf1[abs_NUM_CHANNELS] = {};
	float sampleHoldBuf2[abs_NUM_CHANNELS] = {};
	float sampleHoldBuf3[abs_NUM_CHANNELS] = {};

	// CV-cont cyclic sampler: performance-only, audio-rate, not persisted in patches (see onReset()).
	int cvContMode = 0;// mirrors CVCONT_MODE_PARAM: 0 = off/playback, 1 = record, 2 = pass-through
	bool cvContRecArmed = false;// switched to record but waiting for indexStep==0 to actually start
	bool cvContRecording = false;// actively appending samples to cvContRecBuf
	int cvContRecChannel = 0;// channel locked in at the moment recording was armed/started
	float cvContRecStartValue = 0.0f;// value of the very first sample of the current/last recording pass
	std::vector<float> cvContRecBuf;// in-progress capture, not yet committed
	std::vector<float> cvContBuf[abs_NUM_CHANNELS];// committed per-channel recorded loop (empty = none recorded yet)
	float cvContBufSampleRate[abs_NUM_CHANNELS] = {};// engine sample rate at the moment each buffer was committed (for WAV export)

	// Oscilloscope: a plain rolling trace of the live CV-cont OUTPUT for the active channel -- always
	// reflects whatever is actually playing (recording pass-through, recorded-loop playback, or the
	// legacy per-step curve), independent of how that value was produced.
	float cvContScopeBuf[abs_CVCONT_SCOPE_N] = {};
	int cvContScopeWriteIdx = 0;
	float cvContScopeAccumTime = 0.0f;
	Trigger clockTrigger;
	Trigger resetTrigger;
	Trigger bigTrigger;
	Trigger writeFillTrigger;
	Trigger quantizeBigTrigger;
	Trigger bankTrigger;
	Trigger clearInputTrigger;
	Trigger sampleHoldTrigger[3];
	Trigger pageTrigger[abs_NUM_PAGES];
	Trigger internalSHTriggers1[abs_NUM_CHANNELS];
	Trigger internalSHTriggers2[abs_NUM_CHANNELS];
	Trigger internalSHTriggers3[abs_NUM_CHANNELS];
	dsp::PulseGenerator bigPulse;
	dsp::PulseGenerator eocPulse;


	inline bool getGateBank(int c, int bnk, int s) {return !((gates[c][bnk][s >> 6] & (((uint64_t)1) << (uint64_t)(s & 0x3F))) == 0);}
	inline void setGateBank(int c, int bnk, int s) {gates[c][bnk][s >> 6] |= (((uint64_t)1) << (uint64_t)(s & 0x3F));}
	inline void clearGateBank(int c, int bnk, int s) {gates[c][bnk][s >> 6] &= ~(((uint64_t)1) << (uint64_t)(s & 0x3F));}
	inline void toggleGateBank(int c, int bnk, int s) {gates[c][bnk][s >> 6] ^= (((uint64_t)1) << (uint64_t)(s & 0x3F));}
	inline bool getGate(int c, int s) {return getGateBank(c, bank[c], s);}
	inline void setGate(int c, int s) {setGateBank(c, bank[c], s);}
	inline void clearGate(int c, int s) {clearGateBank(c, bank[c], s);}
	inline void toggleGate(int c, int s) {toggleGateBank(c, bank[c], s);}

	inline float& cv1At(int c, int s) {return cv1[c][bank[c]][s];}
	inline float& cv2At(int c, int s) {return cv2[c][bank[c]][s];}
	inline float& cv3At(int c, int s) {return cv3[c][bank[c]][s];}
	inline float& glAt(int c, int s) {return gl[c][bank[c]][s];}
	inline float& cvContAt(int c, int s) {return cvCont[c][bank[c]][s];}

	inline float readCvContBuf(int c, float phase01) {
		const std::vector<float>& buf = cvContBuf[c];
		size_t n = buf.size();
		if (n == 0) return 0.0f;
		float pos = clamp(phase01, 0.0f, 1.0f) * (float) n;
		int i0 = (int) pos;
		if (i0 >= (int) n) i0 = (int) n - 1;
		int i1 = (i0 + 1) % (int) n;
		float frac = pos - (float) i0;
		return crossfade(buf[i0], buf[i1], frac);
	}

	inline int calcChan() {
		float chanInputValue = inputs[CHAN_INPUT].getVoltage() / 10.0f * (abs_NUM_CHANNELS - 1.0f);
		return (int) clamp(std::round(params[CHAN_PARAM].getValue() + chanInputValue), 0.0f, (abs_NUM_CHANNELS - 1.0f));
	}
	inline int calcLength() {
		float lenInputValue = inputs[LEN_INPUT].isConnected() ? (inputs[LEN_INPUT].getVoltage() / 10.0f * (abs_NUM_STEPS - 1.0f)) : 0.0f;
		return (int) clamp(std::round(params[LEN_PARAM].getValue() + lenInputValue), 0.0f, (abs_NUM_STEPS - 1.0f)) + 1;
	}


	AdvancedButtonSeq() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(RND_PARAM, 0.0f, 100.0f, 0.0f, "Random");
		paramQuantities[RND_PARAM]->snapEnabled = true;
		configParam(CHAN_PARAM, 0.0f, abs_NUM_CHANNELS - 1.0f, 0.0f, "Channel", "", 0.0f, 1.0f, 1.0f);
		paramQuantities[CHAN_PARAM]->snapEnabled = true;
		configParam(LEN_PARAM, 0.0f, abs_NUM_STEPS - 1.0f, 32.0f - 1.0f, "Length", "", 0.0f, 1.0f, 1.0f);
		paramQuantities[LEN_PARAM]->snapEnabled = true;
		configSwitch(DISPMODE_PARAM, 0.0f, 1.0f, 0.0f, "Display mode", {"Length", "Current step"});
		configSwitch(NOTEDISP_PARAM, 0.0f, 1.0f, 1.0f, "CV1 display format", {"Volts", "Notes"});
		configParam(WRITEFILL_PARAM, 0.0f, 1.0f, 0.0f, "Write fill to memory");
		configParam(QUANTIZEBIG_PARAM, 0.0f, 1.0f, 0.0f, "Quantize big button (snap)");
		configParam(BANK_PARAM, 0.0f, 1.0f, 0.0f, "Pattern bank A/B");
		configParam(CLOCK_PARAM, 0.0f, 1.0f, 0.0f, "Clock step");
		configParam(DEL_PARAM, 0.0f, 1.0f, 0.0f, "Delete");
		configParam(RESET_PARAM, 0.0f, 1.0f, 0.0f, "Reset");
		configParam(FILL_PARAM, 0.0f, 1.0f, 0.0f, "Fill");
		configParam(BIG_PARAM, 0.0f, 1.0f, 0.0f, "Big button");
		for (int i = 0; i < 3; i++)
			configParam(SAMPLEHOLD_PARAM + i, 0.0f, 1.0f, 0.0f, string::f("Sample & hold CV%i", i + 1));
		for (int i = 0; i < abs_NUM_PAGES; i++)
			configParam(PAGE_PARAM + i, 0.0f, 1.0f, 0.0f, string::f("Page %i (steps %i-%i)", i + 1, i * abs_STEPS_PER_PAGE + 1, i * abs_STEPS_PER_PAGE + abs_STEPS_PER_PAGE));
		configSwitch(CVCONT_MODE_PARAM, 0.0f, 2.0f, 0.0f, "CV cont cyclic sampler", {"Off / playback", "Record", "Pass-through"});
		getParamQuantity(CVCONT_MODE_PARAM)->randomizeEnabled = false;
		configParam(SCOPE_SPEED_PARAM, 0.0f, 1.0f, 0.5f, "Oscilloscope speed (fully CW = synced to clock)");
		getParamQuantity(SCOPE_SPEED_PARAM)->randomizeEnabled = false;
		configSwitch(GATE_LEN_MODE_PARAM, 0.0f, 1.0f, 0.0f, "Gate in mode", {"Normal (trigger)", "Record gate length"});
		getParamQuantity(GATE_LEN_MODE_PARAM)->randomizeEnabled = false;

		getParamQuantity(DISPMODE_PARAM)->randomizeEnabled = false;
		getParamQuantity(NOTEDISP_PARAM)->randomizeEnabled = false;
		getParamQuantity(LEN_PARAM)->randomizeEnabled = false;
		getParamQuantity(CHAN_PARAM)->randomizeEnabled = false;
		for (int i = 0; i < abs_NUM_PAGES; i++)
			getParamQuantity(PAGE_PARAM + i)->randomizeEnabled = false;

		configInput(CLK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(CHAN_INPUT, "Channel select");
		configInput(LEN_INPUT, "Length");
		configInput(RND_INPUT, "Random");
		configInput(DEL_INPUT, "Delete");
		configInput(FILL_INPUT, "Fill");
		configInput(BIG_INPUT, "Big button");
		configInput(BANK_INPUT, "Bank select trigger");
		configInput(CLEAR_INPUT, "Clear active channel trigger");
		configInput(CV1_INPUT, "CV1 (global, writes active channel)");
		configInput(CV2_INPUT, "CV2 (global, writes active channel)");
		configInput(CV3_INPUT, "CV3 (global, writes active channel)");
		configInput(CVCONT_INPUT, "CV continuous (global, writes active channel)");

		for (int i = 0; i < abs_NUM_CHANNELS; i++) {
			configOutput(CV1_OUTPUT + i, string::f("Channel %i CV1", i + 1));
			configOutput(CV2_OUTPUT + i, string::f("Channel %i CV2", i + 1));
			configOutput(CV3_OUTPUT + i, string::f("Channel %i CV3", i + 1));
			configOutput(CVCONT_OUTPUT + i, string::f("Channel %i CV continuous", i + 1));
			configOutput(GATE_OUTPUT + i, string::f("Channel %i gate", i + 1));
		}
		configOutput(EOC_OUTPUT, "End of cycle");

		onReset();

		loadThemeAndContrastFromDefault(&panelTheme, &panelContrast);
	}


	void onReset() override final {
		indexStep = 0;
		channel = 0;
		page = 0;
		for (int c = 0; c < abs_NUM_CHANNELS; c++) {
			bank[c] = 0;
			for (int b = 0; b < 2; b++) {
				gates[c][b][0] = 0;
				gates[c][b][1] = 0;
				for (int s = 0; s < abs_NUM_STEPS; s++) {
					cv1[c][b][s] = 0.0f;
					cv2[c][b][s] = 0.0f;
					cv3[c][b][s] = 0.0f;
					gl[c][b][s] = 0.5f;
					cvCont[c][b][s] = 0.0f;
				}
			}
		}
		writeFillsToMemory = false;
		quantizeBig = true;
		retrigGatesOnReset = RGOR_NRUN;
		sampleHoldEnabled[0] = sampleHoldEnabled[1] = sampleHoldEnabled[2] = false;
		followPlayhead = false;
		resetCvContSampler();
		resetNonJson();
	}
	// Performance-only state, deliberately not persisted (see class comment above cvContMode) --
	// cleared both on a full module reset and whenever a different patch/preset is loaded into this
	// same module instance (dataFromJson() never carries this data, so stale data must not survive).
	void resetCvContSampler() {
		cvContMode = 0;
		cvContRecArmed = false;
		cvContRecording = false;
		cvContRecChannel = 0;
		cvContRecStartValue = 0.0f;
		cvContRecBuf.clear();
		for (int c = 0; c < abs_NUM_CHANNELS; c++) {
			cvContBuf[c].clear();
			cvContBufSampleRate[c] = 0.0f;
		}
		for (int i = 0; i < abs_CVCONT_SCOPE_N; i++)
			cvContScopeBuf[i] = 0.0f;
		cvContScopeWriteIdx = 0;
		cvContScopeAccumTime = 0.0f;
	}
	void resetNonJson() {
		clockIgnoreOnReset = (long) (clockIgnoreOnResetDuration * APP->engine->getSampleRate());
		lastPeriod = 2.0;
		clockTime = 0.0;
		pendingOp = 0;
		pendingCV1 = pendingCV2 = pendingCV3 = 0.0f;
		gateLenHeld = false;
		gateLenTargetChannel = 0;
		gateLenTargetStep = 0;
		fillPressed = false;
	}


	void onRandomize() override {
		int c = calcChan();
		gates[c][bank[c]][0] = random::u64();
		gates[c][bank[c]][1] = random::u64();
		for (int s = 0; s < abs_NUM_STEPS; s++) {
			cv1At(c, s) = ((float)(random::u32() % 5)) + ((float)(random::u32() % 12)) / 12.0f - 2.0f;
			cv2At(c, s) = 0.0f;
			cv3At(c, s) = 0.0f;
			glAt(c, s) = 0.5f;
		}
	}


	// ***** shared per-step (de)serialization, used by dataToJson/dataFromJson, native file save/load and copy/paste *****
	// The Bank-suffixed versions take an explicit bank (used by dataToJson/dataFromJson to cover
	// both banks); the plain versions operate on the channel's currently active bank (used by
	// copy/paste and native file save/load, matching how BANK scopes everything else).

	json_t* stepsToJsonBank(int chan, int bnk, int startStep, int numSteps) {
		json_t* arr = json_array();
		for (int i = 0; i < numSteps; i++) {
			int s = startStep + i;
			json_t* o = json_object();
			json_object_set_new(o, "gate", json_boolean(getGateBank(chan, bnk, s)));
			json_object_set_new(o, "cv1", json_real(cv1[chan][bnk][s]));
			json_object_set_new(o, "cv2", json_real(cv2[chan][bnk][s]));
			json_object_set_new(o, "cv3", json_real(cv3[chan][bnk][s]));
			json_object_set_new(o, "gl", json_real(gl[chan][bnk][s]));
			json_object_set_new(o, "cvCont", json_real(cvCont[chan][bnk][s]));
			json_array_append_new(arr, o);
		}
		return arr;
	}

	void stepsFromJsonBank(int chan, int bnk, int startStep, json_t* arr, int numSteps) {
		if (!arr || !json_is_array(arr)) return;
		size_t n = json_array_size(arr);
		if (n > (size_t) numSteps) n = (size_t) numSteps;// caller's scope (page vs whole channel) wins over a mismatched clipboard/file
		for (size_t i = 0; i < n; i++) {
			int s = startStep + (int)i;
			if (s < 0 || s >= abs_NUM_STEPS) continue;
			json_t* o = json_array_get(arr, i);
			if (!o) continue;
			json_t* gJ = json_object_get(o, "gate");
			if (gJ) { if (json_is_true(gJ)) setGateBank(chan, bnk, s); else clearGateBank(chan, bnk, s); }
			json_t* c1J = json_object_get(o, "cv1"); if (c1J) cv1[chan][bnk][s] = (float) json_number_value(c1J);
			json_t* c2J = json_object_get(o, "cv2"); if (c2J) cv2[chan][bnk][s] = (float) json_number_value(c2J);
			json_t* c3J = json_object_get(o, "cv3"); if (c3J) cv3[chan][bnk][s] = (float) json_number_value(c3J);
			json_t* glJ = json_object_get(o, "gl"); if (glJ) gl[chan][bnk][s] = (float) json_number_value(glJ);
			json_t* ccJ = json_object_get(o, "cvCont"); if (ccJ) cvCont[chan][bnk][s] = (float) json_number_value(ccJ);
		}
	}

	json_t* stepsToJson(int chan, int startStep, int numSteps) {return stepsToJsonBank(chan, bank[chan], startStep, numSteps);}
	void stepsFromJson(int chan, int startStep, json_t* arr, int numSteps) {stepsFromJsonBank(chan, bank[chan], startStep, arr, numSteps);}

	void clearChannel(int chan) {
		gates[chan][bank[chan]][0] = 0;
		gates[chan][bank[chan]][1] = 0;
		for (int s = 0; s < abs_NUM_STEPS; s++) {
			cv1At(chan, s) = 0.0f;
			cv2At(chan, s) = 0.0f;
			cv3At(chan, s) = 0.0f;
			glAt(chan, s) = 0.5f;
			cvContAt(chan, s) = 0.0f;
		}
	}

	void shiftStepData(int chan, int stepIdx, int dir) {
		int other = stepIdx + dir;
		if (other < 0 || other >= abs_NUM_STEPS) return;
		bool g1 = getGate(chan, stepIdx), g2 = getGate(chan, other);
		if (g2) setGate(chan, stepIdx); else clearGate(chan, stepIdx);
		if (g1) setGate(chan, other); else clearGate(chan, other);
		std::swap(cv1At(chan, stepIdx), cv1At(chan, other));
		std::swap(cv2At(chan, stepIdx), cv2At(chan, other));
		std::swap(cv3At(chan, stepIdx), cv3At(chan, other));
		std::swap(glAt(chan, stepIdx), glAt(chan, other));
		std::swap(cvContAt(chan, stepIdx), cvContAt(chan, other));
	}

	void globalShift(int chan, int dir) {// dir = +1 (shift right / rotate forward) or -1 (shift left / rotate backward)
		if (dir > 0) {
			bool wrapGate = getGate(chan, abs_NUM_STEPS - 1);
			float w1 = cv1At(chan, abs_NUM_STEPS - 1), w2 = cv2At(chan, abs_NUM_STEPS - 1), w3 = cv3At(chan, abs_NUM_STEPS - 1);
			float wg = glAt(chan, abs_NUM_STEPS - 1), wc = cvContAt(chan, abs_NUM_STEPS - 1);
			for (int s = abs_NUM_STEPS - 1; s > 0; s--) {
				if (getGate(chan, s - 1)) setGate(chan, s); else clearGate(chan, s);
				cv1At(chan, s) = cv1At(chan, s - 1); cv2At(chan, s) = cv2At(chan, s - 1); cv3At(chan, s) = cv3At(chan, s - 1);
				glAt(chan, s) = glAt(chan, s - 1); cvContAt(chan, s) = cvContAt(chan, s - 1);
			}
			if (wrapGate) setGate(chan, 0); else clearGate(chan, 0);
			cv1At(chan, 0) = w1; cv2At(chan, 0) = w2; cv3At(chan, 0) = w3; glAt(chan, 0) = wg; cvContAt(chan, 0) = wc;
		}
		else {
			bool wrapGate = getGate(chan, 0);
			float w1 = cv1At(chan, 0), w2 = cv2At(chan, 0), w3 = cv3At(chan, 0), wg = glAt(chan, 0), wc = cvContAt(chan, 0);
			for (int s = 0; s < abs_NUM_STEPS - 1; s++) {
				if (getGate(chan, s + 1)) setGate(chan, s); else clearGate(chan, s);
				cv1At(chan, s) = cv1At(chan, s + 1); cv2At(chan, s) = cv2At(chan, s + 1); cv3At(chan, s) = cv3At(chan, s + 1);
				glAt(chan, s) = glAt(chan, s + 1); cvContAt(chan, s) = cvContAt(chan, s + 1);
			}
			if (wrapGate) setGate(chan, abs_NUM_STEPS - 1); else clearGate(chan, abs_NUM_STEPS - 1);
			cv1At(chan, abs_NUM_STEPS - 1) = w1; cv2At(chan, abs_NUM_STEPS - 1) = w2; cv3At(chan, abs_NUM_STEPS - 1) = w3;
			glAt(chan, abs_NUM_STEPS - 1) = wg; cvContAt(chan, abs_NUM_STEPS - 1) = wc;
		}
	}


	// ***** File I/O (called only from the UI thread, never from process()) *****

	bool saveNativeToFile();
	bool loadNativeFromFile();
	bool saveMidiToFile();
	std::vector<uint8_t> buildMidiTrack(int chan, int numSteps);
	bool saveCvContWaveToFile(int chan);
	bool exportCvContWaveToWav(int chan);
	bool loadCvContWaveFromFile(int chan);


	json_t* dataToJson() override {
		json_t* rootJ = json_object();

		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "panelContrast", json_real(panelContrast));
		json_object_set_new(rootJ, "indexStep", json_integer(indexStep));
		json_object_set_new(rootJ, "channel", json_integer(channel));
		json_object_set_new(rootJ, "page", json_integer(page));
		json_t* bankJ = json_array();
		for (int c = 0; c < abs_NUM_CHANNELS; c++) json_array_append_new(bankJ, json_integer(bank[c]));
		json_object_set_new(rootJ, "bank", bankJ);
		json_object_set_new(rootJ, "writeFillsToMemory", json_boolean(writeFillsToMemory));
		json_object_set_new(rootJ, "quantizeBig", json_boolean(quantizeBig));
		json_object_set_new(rootJ, "retrigGatesOnReset", json_integer(retrigGatesOnReset));
		json_t* shJ = json_array();
		for (int i = 0; i < 3; i++) json_array_append_new(shJ, json_boolean(sampleHoldEnabled[i]));
		json_object_set_new(rootJ, "sampleHoldEnabled", shJ);
		json_object_set_new(rootJ, "followPlayhead", json_boolean(followPlayhead));

		json_t* gatesJ = json_array();
		for (int c = 0; c < abs_NUM_CHANNELS; c++) {
			for (int b = 0; b < 2; b++) {
				json_array_append_new(gatesJ, json_integer((long long) gates[c][b][0]));
				json_array_append_new(gatesJ, json_integer((long long) gates[c][b][1]));
			}
		}
		json_object_set_new(rootJ, "gates", gatesJ);

		json_t* channelsJ = json_array();
		for (int c = 0; c < abs_NUM_CHANNELS; c++) {
			json_t* banksJ = json_array();
			for (int b = 0; b < 2; b++)
				json_array_append_new(banksJ, stepsToJsonBank(c, b, 0, abs_NUM_STEPS));
			json_array_append_new(channelsJ, banksJ);
		}
		json_object_set_new(rootJ, "channels", channelsJ);

		return rootJ;
	}


	void dataFromJson(json_t* rootJ) override {
		json_t* panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ) panelTheme = json_integer_value(panelThemeJ);

		json_t* panelContrastJ = json_object_get(rootJ, "panelContrast");
		if (panelContrastJ) panelContrast = json_number_value(panelContrastJ);

		json_t* indexStepJ = json_object_get(rootJ, "indexStep");
		if (indexStepJ) indexStep = json_integer_value(indexStepJ);

		json_t* channelJ = json_object_get(rootJ, "channel");
		if (channelJ) channel = json_integer_value(channelJ);

		json_t* pageJ = json_object_get(rootJ, "page");
		if (pageJ) page = json_integer_value(pageJ);

		json_t* bankJ = json_object_get(rootJ, "bank");
		if (bankJ) {
			for (int c = 0; c < abs_NUM_CHANNELS; c++) {
				json_t* biJ = json_array_get(bankJ, c);
				if (biJ) bank[c] = json_integer_value(biJ);
			}
		}

		json_t* writeFillsToMemoryJ = json_object_get(rootJ, "writeFillsToMemory");
		if (writeFillsToMemoryJ) writeFillsToMemory = json_is_true(writeFillsToMemoryJ);

		json_t* quantizeBigJ = json_object_get(rootJ, "quantizeBig");
		if (quantizeBigJ) quantizeBig = json_is_true(quantizeBigJ);

		json_t* retrigGatesOnResetJ = json_object_get(rootJ, "retrigGatesOnReset");
		if (retrigGatesOnResetJ) retrigGatesOnReset = json_integer_value(retrigGatesOnResetJ);

		json_t* shJ = json_object_get(rootJ, "sampleHoldEnabled");
		if (shJ) {
			for (int i = 0; i < 3; i++) {
				json_t* shiJ = json_array_get(shJ, i);
				if (shiJ) sampleHoldEnabled[i] = json_is_true(shiJ);
			}
		}

		json_t* followJ = json_object_get(rootJ, "followPlayhead");
		if (followJ) followPlayhead = json_is_true(followJ);

		json_t* gatesJ = json_object_get(rootJ, "gates");
		if (gatesJ) {
			for (int c = 0; c < abs_NUM_CHANNELS; c++) {
				for (int b = 0; b < 2; b++) {
					json_t* g0J = json_array_get(gatesJ, (c * 2 + b) * 2 + 0);
					json_t* g1J = json_array_get(gatesJ, (c * 2 + b) * 2 + 1);
					if (g0J) gates[c][b][0] = (uint64_t)(long long) json_integer_value(g0J);
					if (g1J) gates[c][b][1] = (uint64_t)(long long) json_integer_value(g1J);
				}
			}
		}

		json_t* channelsJ = json_object_get(rootJ, "channels");
		if (channelsJ) {
			for (int c = 0; c < abs_NUM_CHANNELS; c++) {
				json_t* banksJ = json_array_get(channelsJ, c);
				if (!banksJ) continue;
				for (int b = 0; b < 2; b++) {
					json_t* chJ = json_array_get(banksJ, b);
					if (chJ) stepsFromJsonBank(c, b, 0, chJ, abs_NUM_STEPS);
				}
			}
		}

		resetCvContSampler();
		resetNonJson();
	}


	inline void performPending(int chan, float lightTime) {
		if (pendingOp == 1) {
			if (!getGate(chan, indexStep)) {
				setGate(chan, indexStep);
				bigPulse.trigger(0.001f);
			}
			if (inputs[CV1_INPUT].isConnected()) cv1At(chan, indexStep) = pendingCV1;
			if (inputs[CV2_INPUT].isConnected()) cv2At(chan, indexStep) = pendingCV2;
			if (inputs[CV3_INPUT].isConnected()) cv3At(chan, indexStep) = pendingCV3;
		}
		else {
			clearGate(chan, indexStep);
			cv1At(chan, indexStep) = 0.0f;
			cv2At(chan, indexStep) = 0.0f;
			cv3At(chan, indexStep) = 0.0f;
		}
		pendingOp = 0;
	}


	void process(const ProcessArgs& args) override {
		double sampleTime = 1.0 / args.sampleRate;
		static const float lightTime = 0.1f;

		channel = calcChan();
		length = calcLength();
		bool gateLenRecMode = params[GATE_LEN_MODE_PARAM].getValue() > 0.5f;

		//********** CV-cont cyclic sampler: mode transitions (checked every sample) **********
		{
			int newCvContMode = (int) std::round(params[CVCONT_MODE_PARAM].getValue());
			if (newCvContMode != cvContMode) {
				if (newCvContMode == 1) {
					// entering Record: lock the target channel, arm and wait for the top of the loop
					// (or start immediately if we're already there) so a full pass always covers
					// exactly one sequence length, matching the "record == sequence length" behavior.
					cvContRecChannel = channel;
					cvContRecBuf.clear();
					cvContRecording = (indexStep == 0);
					cvContRecArmed = !cvContRecording;
				}
				else {
					// manually left Record before EOC -- abort/discard the in-progress take, keep
					// whatever was already committed from a previous pass (if any)
					cvContRecArmed = false;
					cvContRecording = false;
					cvContRecBuf.clear();
				}
				cvContMode = newCvContMode;
			}
		}

		//********** Buttons, knobs, switches and inputs **********

		if (refresh.processInputs()) {
			// Big button -- in gate-length-record mode, GATE_IN is its own independent trigger (see the
			// gate-length block below) instead of an alternate trigger for this button.
			if (bigTrigger.process(params[BIG_PARAM].getValue() + (gateLenRecMode ? 0.0f : inputs[BIG_INPUT].getVoltage()))) {
				bigLight = 1.0f;
				if (quantizeBig && (clockTime > (lastPeriod / 2.0)) && (clockTime <= (lastPeriod * 1.01))) {
					pendingOp = 1;
					pendingCV1 = inputs[CV1_INPUT].getVoltage();
					pendingCV2 = inputs[CV2_INPUT].getVoltage();
					pendingCV3 = inputs[CV3_INPUT].getVoltage();
				}
				else {
					if (!getGate(channel, indexStep)) {
						setGate(channel, indexStep);
						bigPulse.trigger(0.001f);
					}
					if (inputs[CV1_INPUT].isConnected()) cv1At(channel, indexStep) = inputs[CV1_INPUT].getVoltage();
					if (inputs[CV2_INPUT].isConnected()) cv2At(channel, indexStep) = inputs[CV2_INPUT].getVoltage();
					if (inputs[CV3_INPUT].isConnected()) cv3At(channel, indexStep) = inputs[CV3_INPUT].getVoltage();
				}
			}

			// Write fill to memory (MEM)
			if (writeFillTrigger.process(params[WRITEFILL_PARAM].getValue()))
				writeFillsToMemory = !writeFillsToMemory;

			// Quantize big button (SNAP)
			if (quantizeBigTrigger.process(params[QUANTIZEBIG_PARAM].getValue()))
				quantizeBig = !quantizeBig;

			// Pattern bank A/B (per channel; live-performance toggle, not undo-tracked)
			if (bankTrigger.process(params[BANK_PARAM].getValue() + inputs[BANK_INPUT].getVoltage()))
				bank[channel] = 1 - bank[channel];

			// Clear via CV (live-performance action, like DEL/FILL; not undo-tracked -- the CLEAR
			// button itself, a UI-thread click, still pushes history separately)
			if (clearInputTrigger.process(inputs[CLEAR_INPUT].getVoltage()))
				clearChannel(channel);

			// Sample & hold rows
			for (int i = 0; i < 3; i++) {
				if (sampleHoldTrigger[i].process(params[SAMPLEHOLD_PARAM + i].getValue()))
					sampleHoldEnabled[i] = !sampleHoldEnabled[i];
			}

			// Page select buttons
			for (int i = 0; i < abs_NUM_PAGES; i++) {
				if (pageTrigger[i].process(params[PAGE_PARAM + i].getValue()))
					page = i;
			}

			// Del button
			if (params[DEL_PARAM].getValue() + inputs[DEL_INPUT].getVoltage() > 0.5f) {
				if (quantizeBig && (clockTime > (lastPeriod / 2.0)) && (clockTime <= (lastPeriod * 1.01))) {
					pendingOp = -1;
				}
				else {
					clearGate(channel, indexStep);
					cv1At(channel, indexStep) = 0.0f;
					cv2At(channel, indexStep) = 0.0f;
					cv3At(channel, indexStep) = 0.0f;
				}
			}

			if (pendingOp != 0 && clockTime > (lastPeriod * 1.01))
				performPending(channel, lightTime);
		}// userInputs refresh


		//********** Gate-length-record mode: GATE_IN as an actual gate (checked every sample). Legato:
		// if the hold outlasts the step it started on, the clock-edge handler below ties that step and
		// carries the same gate+CV1/2/3 into the next one, so held notes span steps like a real legato
		// phrase; gateLenTargetStep always tracks whichever step is currently "live" under the hold. **********
		if (gateLenRecMode) {
			float glv = inputs[BIG_INPUT].getVoltage();
			if (!gateLenHeld) {
				if (glv >= 1.0f) {
					// rising edge: write the step now (gate + CV1/2/3), same as a direct BIG press
					if (!getGate(channel, indexStep)) {
						setGate(channel, indexStep);
						bigPulse.trigger(0.001f);
					}
					if (inputs[CV1_INPUT].isConnected()) cv1At(channel, indexStep) = inputs[CV1_INPUT].getVoltage();
					if (inputs[CV2_INPUT].isConnected()) cv2At(channel, indexStep) = inputs[CV2_INPUT].getVoltage();
					if (inputs[CV3_INPUT].isConnected()) cv3At(channel, indexStep) = inputs[CV3_INPUT].getVoltage();
					bigLight = 1.0f;
					gateLenTargetChannel = channel;
					gateLenTargetStep = indexStep;
					gateLenHeld = true;
				}
			}
			else if (glv <= 0.1f) {
				// falling edge: elapsed time since the current target step's own clock edge (clockTime),
				// as a fraction of one step's period, becomes its recorded gate length (>=~1 step long
				// reads as Tie/Glide, same as the "G" knob at max)
				glAt(gateLenTargetChannel, gateLenTargetStep) = clamp((float)(clockTime / (lastPeriod > 1e-6 ? lastPeriod : 1e-6)), 0.0f, 1.0f);
				gateLenHeld = false;
			}
		}
		else if (gateLenHeld) {
			gateLenHeld = false;// mode switched away mid-hold -- abandon without finalizing the length
		}


		//********** Clock and reset **********

		if (clockIgnoreOnReset == 0l) {
			if (clockTrigger.process(inputs[CLK_INPUT].getVoltage() + params[CLOCK_PARAM].getValue())) {
				bool wrapped = ((indexStep + 1) >= length);

				// CV-cont cyclic sampler: a recording pass always ends exactly at EOC, so it always
				// covers exactly one sequence length regardless of when it was armed/started.
				if (cvContRecording && wrapped) {
					cvContBuf[cvContRecChannel] = std::move(cvContRecBuf);
					cvContBufSampleRate[cvContRecChannel] = (float) args.sampleRate;
					cvContRecBuf.clear();
					cvContRecording = false;
					cvContRecArmed = false;
					params[CVCONT_MODE_PARAM].setValue(0.0f);
					cvContMode = 0;
				}

				// Gate-length legato: the step under the hold survived a full clock edge, so it was
				// held for its entire duration -- tie it, then carry the same held note into the step
				// we're about to enter.
				if (gateLenHeld)
					glAt(gateLenTargetChannel, gateLenTargetStep) = 1.0f;

				indexStep = wrapped ? 0 : (indexStep + 1);
				if (wrapped)
					eocPulse.trigger(0.001f);

				if (gateLenHeld) {
					int c = gateLenTargetChannel;
					if (!getGate(c, indexStep)) {
						setGate(c, indexStep);
						if (c == channel) bigPulse.trigger(0.001f);
					}
					cv1At(c, indexStep) = cv1At(c, gateLenTargetStep);
					cv2At(c, indexStep) = cv2At(c, gateLenTargetStep);
					cv3At(c, indexStep) = cv3At(c, gateLenTargetStep);
					gateLenTargetStep = indexStep;
				}

				// CV-cont cyclic sampler: an armed recording starts the instant we land on step 0
				if (cvContRecArmed && indexStep == 0) {
					cvContRecArmed = false;
					cvContRecording = true;
				}

				if (followPlayhead)
					page = indexStep / abs_STEPS_PER_PAGE;

				// CV continuous: always recorded, independent of gate state
				if (inputs[CVCONT_INPUT].isConnected())
					cvContAt(channel, indexStep) = inputs[CVCONT_INPUT].getVoltage();

				// Fill button
				fillPressed = (params[FILL_PARAM].getValue() + inputs[FILL_INPUT].getVoltage()) > 0.5f;
				if (fillPressed && writeFillsToMemory) {
					setGate(channel, indexStep);
					if (inputs[CV1_INPUT].isConnected()) cv1At(channel, indexStep) = inputs[CV1_INPUT].getVoltage();
					if (inputs[CV2_INPUT].isConnected()) cv2At(channel, indexStep) = inputs[CV2_INPUT].getVoltage();
					if (inputs[CV3_INPUT].isConnected()) cv3At(channel, indexStep) = inputs[CV3_INPUT].getVoltage();
				}

				if (pendingOp != 0)
					performPending(channel, lightTime);

				// Random (toggle gate according to probability knob)
				float rnd01 = params[RND_PARAM].getValue() / 100.0f + inputs[RND_INPUT].getVoltage() / 10.0f;
				if (rnd01 > 0.0f) {
					if (random::uniform() < rnd01)
						toggleGate(channel, indexStep);
				}
				lastPeriod = clockTime > 2.0 ? 2.0 : clockTime;
				clockTime = 0.0;
			}
		}


		// Reset
		if (resetTrigger.process(params[RESET_PARAM].getValue() + inputs[RESET_INPUT].getVoltage())) {
			clockIgnoreOnReset = (long) (clockIgnoreOnResetDuration * args.sampleRate);
			// Gate-length legato: treat an external reset like a clock edge for a note currently held
			// under GATE_IN -- tie the interrupted step and carry the hold into the new step 0.
			if (gateLenHeld)
				glAt(gateLenTargetChannel, gateLenTargetStep) = 1.0f;
			indexStep = 0;
			if (gateLenHeld) {
				int c = gateLenTargetChannel;
				if (!getGate(c, indexStep)) {
					setGate(c, indexStep);
					if (c == channel) bigPulse.trigger(0.001f);
				}
				cv1At(c, indexStep) = cv1At(c, gateLenTargetStep);
				cv2At(c, indexStep) = cv2At(c, gateLenTargetStep);
				cv3At(c, indexStep) = cv3At(c, gateLenTargetStep);
				gateLenTargetStep = indexStep;
			}
			if (followPlayhead)
				page = 0;
			clockTrigger.reset();
			// An external sequence reset restarts an in-progress recording cleanly from the top
			// (indexStep is already 0 here) instead of leaving it to finalize on a broken loop.
			if (cvContRecording || cvContRecArmed) {
				cvContRecBuf.clear();
				cvContRecArmed = false;
				cvContRecording = true;
			}
		}


		// CV-cont cyclic sampler: audio-rate capture, independent of the clock -- this is what makes
		// the recorded loop a true waveform instead of another per-step staircase.
		if (cvContRecording && cvContRecBuf.size() < abs_CVCONT_REC_MAX_SAMPLES) {
			float v = inputs[CVCONT_INPUT].getVoltage();
			if (cvContRecBuf.empty())
				cvContRecStartValue = v;// remembered for the scope's "loop start" marker
			cvContRecBuf.push_back(v);
		}


		//********** Outputs and lights **********

		// Gate timing is driven by elapsed time since the last clock edge (stepPhase), not by the
		// raw clock input's own pulse width -- this way per-step Gate Length works correctly with
		// both narrow trigger-style clocks and wide square-wave clocks, as expected of a GL knob.
		float rawPhase = (float)(clockTime / (lastPeriod > 1e-6 ? lastPeriod : 1e-6));
		float stepPhase = clamp(rawPhase, 0.0f, 1.0f);
		bool bigPulseState = bigPulse.process((float)sampleTime);
		bool retriggingOnReset = (clockIgnoreOnReset != 0l && retrigGatesOnReset != RGOR_NONE);

		int nextIndexStep = (indexStep + 1 >= length) ? 0 : (indexStep + 1);// respects the active LENGTH's loop point, not just the raw 128-step buffer

		// Oscilloscope sample spacing: SCOPE_SPEED_PARAM sweeps the rolling window from slow (long,
		// zoomed out) to fast (short, zoomed in); fully clockwise locks it to the clock so exactly one
		// sequence loop always fills the display width, regardless of tempo.
		float scopeSpeed = params[SCOPE_SPEED_PARAM].getValue();
		float scopeSampleDt;
		if (scopeSpeed >= 0.97f) {
			double loopDur = (double) length * (lastPeriod > 1e-6 ? lastPeriod : 1e-6);
			scopeSampleDt = clamp((float)(loopDur / (double) abs_CVCONT_SCOPE_N), 0.0002f, 1.0f);
		}
		else {
			static const float SCOPE_WIN_MIN = 0.5f, SCOPE_WIN_MAX = 15.0f;
			float t = scopeSpeed / 0.97f;// fast (t=1) .. slow (t=0)
			float windowSec = SCOPE_WIN_MAX * std::pow(SCOPE_WIN_MIN / SCOPE_WIN_MAX, t);
			scopeSampleDt = windowSec / (float) abs_CVCONT_SCOPE_N;
		}

		for (int i = 0; i < abs_NUM_CHANNELS; i++) {
			bool gate = getGate(i, indexStep);
			float glv = glAt(i, indexStep);
			bool tie = gate && glv >= 0.999f;
			bool phaseGateOn = gate && (tie || stepPhase < glv);
			bool fillHit = (i == channel && fillPressed && stepPhase < 0.5f);// clock-synced retriggered hit while FILL is held
			bool bigHit = (i == channel && bigPulseState && gate);
			bool gateOut = phaseGateOn || fillHit || bigHit;
			float gateOutValue = gateOut ? 10.0f : 0.0f;

			// CV1: glide into next step while tied, else per-step value (with optional S&H)
			float cv1Out;
			if (tie) {
				cv1Out = getGate(i, nextIndexStep) ? crossfade(cv1At(i, indexStep), cv1At(i, nextIndexStep), stepPhase) : cv1At(i, indexStep);
			}
			else {
				if (internalSHTriggers1[i].process(gateOutValue))
					sampleHoldBuf1[i] = cv1At(i, indexStep);
				cv1Out = sampleHoldEnabled[0] ? sampleHoldBuf1[i] : cv1At(i, indexStep);
			}

			if (internalSHTriggers2[i].process(gateOutValue))
				sampleHoldBuf2[i] = cv2At(i, indexStep);
			float cv2Out = sampleHoldEnabled[1] ? sampleHoldBuf2[i] : cv2At(i, indexStep);

			if (internalSHTriggers3[i].process(gateOutValue))
				sampleHoldBuf3[i] = cv3At(i, indexStep);
			float cv3Out = sampleHoldEnabled[2] ? sampleHoldBuf3[i] : cv3At(i, indexStep);

			outputs[CV1_OUTPUT + i].setVoltage(cv1Out);
			outputs[CV2_OUTPUT + i].setVoltage(cv2Out);
			outputs[CV3_OUTPUT + i].setVoltage(cv3Out);

			// CV cont: while this channel is being recorded (or is in pass-through mode), mirror the
			// live input; otherwise play back its own recorded loop if it has one (phase-locked to the
			// running sequence), else fall back to the legacy per-step interpolated curve.
			float cvContOut;
			if (cvContRecording && i == cvContRecChannel) {
				cvContOut = inputs[CVCONT_INPUT].getVoltage();
			}
			else if (cvContMode == 2 && i == channel) {
				cvContOut = inputs[CVCONT_INPUT].getVoltage();
			}
			else if (!cvContBuf[i].empty()) {
				float phase01 = length > 0 ? ((float) indexStep + stepPhase) / (float) length : 0.0f;
				cvContOut = readCvContBuf(i, phase01);
			}
			else {
				cvContOut = crossfade(cvContAt(i, indexStep), cvContAt(i, nextIndexStep), stepPhase);
			}
			outputs[CVCONT_OUTPUT + i].setVoltage(cvContOut);
			outputs[GATE_OUTPUT + i].setVoltage(retriggingOnReset ? 0.0f : gateOutValue);

			// Oscilloscope tap: sample the active channel's actual output, decimated per scopeSampleDt
			if (i == channel) {
				cvContScopeAccumTime += (float) sampleTime;
				if (cvContScopeAccumTime >= scopeSampleDt) {
					cvContScopeAccumTime -= scopeSampleDt;
					cvContScopeBuf[cvContScopeWriteIdx] = cvContOut;
					cvContScopeWriteIdx = (cvContScopeWriteIdx + 1) % abs_CVCONT_SCOPE_N;
				}
			}
		}

		outputs[EOC_OUTPUT].setVoltage(eocPulse.process((float)sampleTime) ? 10.0f : 0.0f);


		// lights
		if (refresh.processLights()) {
			float deltaTime = (float)sampleTime * RefreshCounter::displayRefreshStepSkips;

			for (int i = 0; i < abs_NUM_CHANNELS; i++)
				lights[CHAN_LIGHT + i].setBrightness(i == channel ? 1.0f : 0.0f);

			deltaTime = (float)sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2);

			lights[BIGC_LIGHT].setSmoothBrightness(bigLight, deltaTime);
			bigLight = 0.0f;

			lights[WRITEFILL_LIGHT].setBrightness(writeFillsToMemory ? 1.0f : 0.0f);
			lights[QUANTIZEBIG_LIGHT].setBrightness(quantizeBig ? 1.0f : 0.0f);
			lights[BANK_LIGHT + 0].setBrightness(bank[channel] == 0 ? 1.0f : 0.0f);
			lights[BANK_LIGHT + 1].setBrightness(bank[channel] == 1 ? 1.0f : 0.0f);
			for (int i = 0; i < 3; i++)
				lights[SAMPLEHOLD_LIGHT + i].setBrightness(sampleHoldEnabled[i] ? 1.0f : 0.0f);
			for (int i = 0; i < abs_NUM_PAGES; i++)
				lights[PAGE_LIGHT + i].setBrightness(i == page ? 1.0f : 0.0f);
			lights[EOC_LIGHT].setSmoothBrightness(outputs[EOC_OUTPUT].getVoltage() > 1.0f ? 1.0f : 0.0f, deltaTime);
		}

		clockTime += sampleTime;

		if (clockIgnoreOnReset > 0l)
			clockIgnoreOnReset--;
	}// process()
};
