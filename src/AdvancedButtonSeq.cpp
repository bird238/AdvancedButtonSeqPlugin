//***********************************************************************************************
//Advanced Button Seq: a 6-channel, 128-step graphical sequencer for VCV Rack
//
//GUI widget, file I/O (native format + MIDI export) and model registration.
//The sequencer engine itself lives in AdvancedButtonSeq.hpp.
//
//See ./LICENSE.md for all licenses
//***********************************************************************************************

#include "AdvancedButtonSeq.hpp"
#include <osdialog.h>
#include <cstdio>
#include <cstring>
#include <algorithm>


// ***************************************************************************************
// Panel layout constants (generated together with res/panels/AdvancedButtonSeq.svg by the
// same script, so the SVG art and the widget coordinates below can never drift apart).
// ***************************************************************************************

static constexpr float PANEL_W_MM = 325.120f;
static constexpr float PANEL_H_MM = 128.500f;
static constexpr float col1_x0 = 2.570f, col1_x1 = 55.576f;
static constexpr float xA_L = 11.404f, xA_C = 29.073f, xA_R = 46.742f;
static constexpr float cvin_xs[4] = {9.196f, 20.896f, 32.596f, 44.296f};
static constexpr float knobrow_y0 = 11.340f, knobrow_y1 = 36.340f;
static constexpr float chled_y0 = 38.107f;
static constexpr float bigbank_y0 = 42.274f, bigbank_y1 = 55.774f;
static constexpr float clkrow_y0 = 57.541f;
static constexpr float fillrow_y0 = 75.608f;
static constexpr float clearrow_y0 = 93.674f, clearrow_y1 = 108.674f;
static constexpr float cvin_y0 = 112.130f, cvin_y1 = 125.930f;
static constexpr float cvcont_sw_x = 51.890f, cvcont_sw_y = 119.600f;
static constexpr float col2_content_x0 = 62.322f, col2_content_x1 = 242.865f;
static constexpr float c2_hdr_y1 = 11.343f;
static constexpr float c2_page_y0 = 12.943f, c2_page_y1 = 20.443f;
static constexpr float PAGE_BTN_Y = c2_page_y0 + 2.6f;
static constexpr float c2_action_y0 = 119.930f;
static constexpr float ACTION_W_MM = 28.424f, ACTION_H_MM = 6.000f;
static constexpr float action_xs[6] = {62.322f, 92.746f, 123.170f, 153.594f, 184.017f, 214.441f};
static constexpr float page_xs[8] = {73.606f, 96.174f, 118.742f, 141.310f, 163.878f, 186.445f, 209.013f, 231.581f};
static constexpr float gridX0 = 62.322f, gridY0 = 22.043f, cellW = 44.296f, cellH = 23.232f, GAP_CELL = 1.120f;
static constexpr float CELL_NUM_Y = 2.200f, CELL_SHIFT_Y0 = 5.000f, CELL_SHIFT_H = 7.710f, CELL_SHIFT_W = 3.200f;
static constexpr float CELL_KNOB_Y = 6.900f, CELL_KNOB_PITCH = 8.600f;
static constexpr float CELL_KNOBLABEL_Y = 11.900f;
static constexpr float CELL_CV1_Y = 16.400f, CELL_CV1_H = 5.500f;
static constexpr float col3_content_x0 = 249.611f, col3_content_x1 = 322.535f;
static constexpr float c3_hdr_y1 = 11.343f, c3_colhdr_y = 15.593f;
static constexpr float chan_xs[6] = {265.727f, 274.187f, 282.646f, 291.106f, 299.566f, 308.025f};
static constexpr float matrix_row_y[5] = {25.628f, 38.198f, 50.768f, 63.338f, 75.908f};
static constexpr float sh_x = 317.395f, eoc_y = 92.478f;
static constexpr float infobox_y0 = 102.763f, infobox_y1 = 125.930f;


// Static panel labels (NanoSVG, used by Rack for panel backgrounds, does not render <text>
// elements -- so all panel legends are drawn at runtime instead, from this generated table).
struct PanelLabel {
	float x, y;
	const char* text;
	float size;
	int align;// 0 = left, 1 = center, 2 = right
	int font;// 0 = Nunito (bold UI label), 2 = Share Tech Mono, 3 = DSEG7 (7-segment)
};
#include "AdvancedButtonSeqPanelLabels.inc"

static std::shared_ptr<Font> loadABSFont(int kind) {
	if (kind == 2) return APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/ShareTechMono-Regular.ttf"));
	if (kind == 3) return APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/DSEG7ClassicMini-Bold.ttf"));
	return APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Nunito-Bold.ttf"));
}
static const NVGcolor ABS_INK = nvgRGB(0x23, 0x26, 0x2a);
static const NVGcolor ABS_ACC = nvgRGB(0xa6, 0xff, 0x3f);
static const NVGcolor ABS_DISP_BG = nvgRGB(0x0c, 0x15, 0x0b);


// ***************************************************************************************
// MIDI export (Standard MIDI File, format 0, single track, 96 PPQ, 16th-note steps)
// ***************************************************************************************

static void midiPushVarLen(std::vector<uint8_t>& buf, uint32_t value) {
	uint32_t buffer = value & 0x7F;
	while ((value >>= 7)) {
		buffer <<= 8;
		buffer |= ((value & 0x7F) | 0x80);
	}
	while (true) {
		buf.push_back((uint8_t)(buffer & 0xFF));
		if (buffer & 0x80) buffer >>= 8;
		else break;
	}
}

struct MidiEvent {
	uint32_t tick;
	bool isOn;
	int note;
	int vel;
};

std::vector<uint8_t> AdvancedButtonSeq::buildMidiTrack(int chan, int numSteps) {
	static const uint32_t ticksPerStep = 24;// 96 PPQ / 4 (16th-note steps)
	std::vector<MidiEvent> events;

	int s = 0;
	uint32_t tick = 0;
	while (s < numSteps) {
		if (getGate(chan, s)) {
			int note = 60 + (int) std::round(cv1At(chan, s) * 12.0f);
			note = clamp(note, 0, 127);
			uint32_t startTick = tick;
			uint32_t durTicks;
			int mergedSteps = 1;
			if (glAt(chan, s) >= 0.999f) {
				// Tie: merge consecutive tied+gated steps into one longer note
				int look = s;
				while (glAt(chan, look) >= 0.999f && (look + 1) < numSteps && getGate(chan, look + 1)) {
					look++;
					mergedSteps++;
				}
				durTicks = ticksPerStep * (uint32_t) mergedSteps;
			}
			else {
				durTicks = (uint32_t) clamp((int) std::round(glAt(chan, s) * ticksPerStep), 1, (int) ticksPerStep);
			}
			events.push_back({startTick, true, note, 100});
			events.push_back({startTick + durTicks, false, note, 0});
			tick += ticksPerStep * (uint32_t) mergedSteps;
			s += mergedSteps;
		}
		else {
			tick += ticksPerStep;
			s++;
		}
	}

	std::stable_sort(events.begin(), events.end(), [](const MidiEvent& a, const MidiEvent& b) {
		if (a.tick != b.tick) return a.tick < b.tick;
		return (!a.isOn) && b.isOn;// note-offs before note-ons at the same tick
	});

	std::vector<uint8_t> track;
	uint32_t lastTick = 0;
	for (const MidiEvent& ev : events) {
		midiPushVarLen(track, ev.tick - lastTick);
		lastTick = ev.tick;
		track.push_back((uint8_t)(ev.isOn ? 0x90 : 0x80));
		track.push_back((uint8_t) ev.note);
		track.push_back((uint8_t) ev.vel);
	}
	// end of track meta event
	midiPushVarLen(track, 0);
	track.push_back(0xFF);
	track.push_back(0x2F);
	track.push_back(0x00);

	return track;
}

bool AdvancedButtonSeq::saveMidiToFile() {
	// No filters passed here (unlike the native/wave dialogs below): this dialog crashes on cancel
	// on at least one Linux setup, and the file-type filter dropdown is the most likely GTK/osdialog
	// interaction left to rule out. Cosmetic loss only if this doesn't help: no .mid-only filtering.
	char* path = osdialog_file(OSDIALOG_SAVE, NULL, "AdvancedButtonSeq.mid", NULL);
	if (!path) return false;

	std::vector<uint8_t> track = buildMidiTrack(channel, length);

	FILE* f = fopen(path, "wb");
	free(path);
	if (!f) return false;

	uint8_t header[14] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96};
	fwrite(header, 1, 14, f);

	uint8_t trackHeader[8];
	trackHeader[0] = 'M'; trackHeader[1] = 'T'; trackHeader[2] = 'r'; trackHeader[3] = 'k';
	uint32_t len = (uint32_t) track.size();
	trackHeader[4] = (uint8_t)((len >> 24) & 0xFF);
	trackHeader[5] = (uint8_t)((len >> 16) & 0xFF);
	trackHeader[6] = (uint8_t)((len >> 8) & 0xFF);
	trackHeader[7] = (uint8_t)(len & 0xFF);
	fwrite(trackHeader, 1, 8, f);
	fwrite(track.data(), 1, track.size(), f);
	fclose(f);
	return true;
}


// ***************************************************************************************
// Native format save/load (JSON, full 128-step channel, via osdialog)
// ***************************************************************************************

bool AdvancedButtonSeq::saveNativeToFile() {
	osdialog_filters* filters = osdialog_filters_parse("Advanced Button Seq:abseq");
	char* path = osdialog_file(OSDIALOG_SAVE, NULL, "AdvancedButtonSeq.abseq", filters);
	osdialog_filters_free(filters);
	if (!path) return false;

	json_t* root = json_object();
	json_object_set_new(root, "AdvancedButtonSeqFormat", json_integer(1));
	json_object_set_new(root, "channel", json_integer(channel));
	json_object_set_new(root, "bank", json_integer(bank[channel]));
	json_object_set_new(root, "length", json_integer(length));
	json_object_set_new(root, "steps", stepsToJson(channel, 0, abs_NUM_STEPS));

	FILE* f = fopen(path, "w");
	free(path);
	bool ok = false;
	if (f) {
		json_dumpf(root, f, JSON_INDENT(2));
		fclose(f);
		ok = true;
	}
	json_decref(root);
	return ok;
}

bool AdvancedButtonSeq::loadNativeFromFile() {
	osdialog_filters* filters = osdialog_filters_parse("Advanced Button Seq:abseq");
	char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
	osdialog_filters_free(filters);
	if (!path) return false;

	FILE* f = fopen(path, "r");
	free(path);
	if (!f) return false;

	json_error_t error;
	json_t* root = json_loadf(f, 0, &error);
	fclose(f);
	if (!root) return false;

	json_t* stepsJ = json_object_get(root, "steps");
	if (stepsJ) stepsFromJson(channel, 0, stepsJ, abs_NUM_STEPS);

	json_t* lenJ = json_object_get(root, "length");
	if (lenJ) {
		float newLen = clamp((float) json_integer_value(lenJ) - 1.0f, 0.0f, (float)(abs_NUM_STEPS - 1));
		params[LEN_PARAM].setValue(newLen);
	}

	json_decref(root);
	return true;
}


// ***************************************************************************************
// CV-cont cyclic sampler: save the recorded waveform as its own file (native JSON, reusable
// only by this module) or export it as a standard WAV file (usable anywhere else)
// ***************************************************************************************

bool AdvancedButtonSeq::saveCvContWaveToFile(int chan) {
	if (chan < 0 || chan >= abs_NUM_CHANNELS || cvContBuf[chan].empty()) return false;

	osdialog_filters* filters = osdialog_filters_parse("CV cont wave:abscv");
	char* path = osdialog_file(OSDIALOG_SAVE, NULL, "AdvancedButtonSeq-CVcont.abscv", filters);
	osdialog_filters_free(filters);
	if (!path) return false;

	json_t* root = json_object();
	json_object_set_new(root, "AdvancedButtonSeqCvContWave", json_integer(1));
	json_object_set_new(root, "channel", json_integer(chan));
	json_object_set_new(root, "sampleRate", json_real(cvContBufSampleRate[chan]));
	json_t* arr = json_array();
	for (float v : cvContBuf[chan])
		json_array_append_new(arr, json_real(v));
	json_object_set_new(root, "samples", arr);

	FILE* f = fopen(path, "w");
	free(path);
	bool ok = false;
	if (f) {
		json_dumpf(root, f, JSON_COMPACT);
		fclose(f);
		ok = true;
	}
	json_decref(root);
	return ok;
}

bool AdvancedButtonSeq::exportCvContWaveToWav(int chan) {
	if (chan < 0 || chan >= abs_NUM_CHANNELS || cvContBuf[chan].empty()) return false;

	osdialog_filters* filters = osdialog_filters_parse("WAV audio:wav");
	char* path = osdialog_file(OSDIALOG_SAVE, NULL, "AdvancedButtonSeq-CVcont.wav", filters);
	osdialog_filters_free(filters);
	if (!path) return false;

	FILE* f = fopen(path, "wb");
	free(path);
	if (!f) return false;

	const std::vector<float>& buf = cvContBuf[chan];
	uint32_t numSamples = (uint32_t) buf.size();
	uint32_t sampleRate = (uint32_t) std::round(cvContBufSampleRate[chan] > 0.f ? cvContBufSampleRate[chan] : 48000.f);
	uint16_t numChannels = 1;
	uint16_t bitsPerSample = 16;
	uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
	uint16_t blockAlign = (uint16_t)(numChannels * bitsPerSample / 8);
	uint32_t dataSize = numSamples * blockAlign;
	uint32_t chunkSize = 36 + dataSize;
	uint32_t fmtChunkSize = 16;
	uint16_t audioFormat = 1;// PCM

	// WAV is little-endian, matching this build's native x86-64 byte order, so these multi-byte
	// fields can be written directly without manual byte-swapping.
	fwrite("RIFF", 1, 4, f);
	fwrite(&chunkSize, 4, 1, f);
	fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f);
	fwrite(&fmtChunkSize, 4, 1, f);
	fwrite(&audioFormat, 2, 1, f);
	fwrite(&numChannels, 2, 1, f);
	fwrite(&sampleRate, 4, 1, f);
	fwrite(&byteRate, 4, 1, f);
	fwrite(&blockAlign, 2, 1, f);
	fwrite(&bitsPerSample, 2, 1, f);
	fwrite("data", 1, 4, f);
	fwrite(&dataSize, 4, 1, f);

	for (uint32_t i = 0; i < numSamples; i++) {
		// Eurorack +/-10V CV convention mapped to the WAV [-1, 1] range
		float v = clamp(buf[i] / 10.f, -1.f, 1.f);
		int16_t s = (int16_t) std::round(v * 32767.f);
		fwrite(&s, 2, 1, f);
	}

	fclose(f);
	return true;
}

bool AdvancedButtonSeq::loadCvContWaveFromFile(int chan) {
	if (chan < 0 || chan >= abs_NUM_CHANNELS) return false;

	osdialog_filters* filters = osdialog_filters_parse("CV cont wave:wav,abscv");
	char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
	osdialog_filters_free(filters);
	if (!path) return false;

	FILE* f = fopen(path, "rb");
	free(path);
	if (!f) return false;

	char magic[4] = {};
	bool haveMagic = fread(magic, 1, 4, f) == 4;
	fseek(f, 0, SEEK_SET);
	bool isWav = haveMagic && magic[0] == 'R' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == 'F';

	std::vector<float> samples;
	float sampleRate = 48000.f;

	if (isWav) {
		fseek(f, 12, SEEK_SET);// skip "RIFF" + chunkSize + "WAVE"
		uint16_t audioFormat = 0, numChannels = 1, bitsPerSample = 16;
		uint32_t fileSampleRate = 48000;
		bool haveFmt = false;
		while (true) {
			char chunkId[4];
			uint32_t chunkSize;
			if (fread(chunkId, 1, 4, f) != 4) break;
			if (fread(&chunkSize, 4, 1, f) != 1) break;
			long chunkStart = ftell(f);
			if (memcmp(chunkId, "fmt ", 4) == 0) {
				fread(&audioFormat, 2, 1, f);
				fread(&numChannels, 2, 1, f);
				fread(&fileSampleRate, 4, 1, f);
				fseek(f, 6, SEEK_CUR);// skip byteRate(4) + blockAlign(2)
				fread(&bitsPerSample, 2, 1, f);
				haveFmt = true;
			}
			else if (memcmp(chunkId, "data", 4) == 0 && haveFmt && numChannels > 0 && bitsPerSample > 0) {
				uint32_t bytesPerSample = bitsPerSample / 8;
				uint32_t frameSize = bytesPerSample * numChannels;
				uint32_t numFrames = frameSize > 0 ? chunkSize / frameSize : 0;
				samples.reserve(numFrames);
				for (uint32_t i = 0; i < numFrames; i++) {
					float v = 0.f;
					if (audioFormat == 3 && bitsPerSample == 32) {
						float s; fread(&s, 4, 1, f);
						v = s;
					}
					else if (bitsPerSample == 16) {
						int16_t s; fread(&s, 2, 1, f);
						v = (float) s / 32768.f;
					}
					else if (bitsPerSample == 8) {
						uint8_t s; fread(&s, 1, 1, f);
						v = ((float) s - 128.f) / 128.f;
					}
					else if (bitsPerSample == 24) {
						uint8_t b[3]; fread(b, 1, 3, f);
						int32_t s = (int32_t)((b[2] << 24) | (b[1] << 16) | (b[0] << 8));
						v = (float)(s >> 8) / 8388608.f;
					}
					else if (bitsPerSample == 32) {
						int32_t s; fread(&s, 4, 1, f);
						v = (float) s / 2147483648.f;
					}
					if (numChannels > 1)
						fseek(f, (long)(bytesPerSample * (numChannels - 1)), SEEK_CUR);// mono-ify: keep only the first channel
					samples.push_back(clamp(v, -1.f, 1.f) * 10.f);// WAV [-1,1] -> Eurorack +/-10V CV convention
				}
				sampleRate = (float) fileSampleRate;
				break;
			}
			fseek(f, chunkStart + (long) chunkSize + (long)(chunkSize & 1), SEEK_SET);// chunks are word-aligned
		}
	}
	else {
		json_error_t error;
		json_t* root = json_loadf(f, 0, &error);
		if (root) {
			json_t* srJ = json_object_get(root, "sampleRate");
			if (srJ) sampleRate = (float) json_number_value(srJ);
			json_t* arrJ = json_object_get(root, "samples");
			if (arrJ && json_is_array(arrJ)) {
				size_t n = json_array_size(arrJ);
				samples.reserve(n);
				for (size_t i = 0; i < n; i++)
					samples.push_back((float) json_number_value(json_array_get(arrJ, i)));
			}
			json_decref(root);
		}
	}

	fclose(f);
	if (samples.empty()) return false;

	cvContBuf[chan] = std::move(samples);
	cvContBufSampleRate[chan] = sampleRate;
	return true;
}


// ***************************************************************************************
// Widget
// ***************************************************************************************

static void pushModuleHistory(AdvancedButtonSeq* module, std::string name, json_t* oldModuleJ) {
	if (!module || !oldModuleJ) return;
	history::ModuleChange* h = new history::ModuleChange;
	h->name = name;
	h->moduleId = module->id;
	h->oldModuleJ = oldModuleJ;
	h->newModuleJ = module->toJson();
	APP->history->push(h);
}


struct AdvancedButtonSeqWidget : ModuleWidget {

	// ---- static panel legends (baked positions/text from the generator, drawn at runtime) ----
	struct PanelLabelsWidget : TransparentWidget {
		void draw(const DrawArgs& args) override {
			std::shared_ptr<Font> fonts[3] = {loadABSFont(0), loadABSFont(2), loadABSFont(3)};
			for (const PanelLabel& lbl : panelLabels) {
				int fi = (lbl.font == 3) ? 2 : (lbl.font == 2 ? 1 : 0);
				if (!fonts[fi]) continue;
				nvgFontFaceId(args.vg, fonts[fi]->handle);
				nvgFontSize(args.vg, mm2px(lbl.size));
				nvgTextAlign(args.vg, (lbl.align == 0 ? NVG_ALIGN_LEFT : (lbl.align == 1 ? NVG_ALIGN_CENTER : NVG_ALIGN_RIGHT)) | NVG_ALIGN_BASELINE);
				nvgFillColor(args.vg, lbl.font == 3 ? ABS_ACC : ABS_INK);
				nvgText(args.vg, mm2px(lbl.x), mm2px(lbl.y), lbl.text, NULL);
			}
		}
	};


	// ---- generic numeric text-entry field for right-click menus ----
	struct StepValueField : ui::TextField {
		AdvancedButtonSeq* module = nullptr;
		float* valueSrc = nullptr;
		float minV = -10.f, maxV = 10.f;
		json_t* preJ = nullptr;

		void step() override {
			APP->event->setSelectedWidget(this);
			TextField::step();
		}
		void setup(AdvancedButtonSeq* m, float* v, float mn, float mx) {
			module = m; valueSrc = v; minV = mn; maxV = mx;
			text = string::f("%.*g", 5, math::normalizeZero(*v));
			selectAll();
			preJ = module->toJson();
		}
		void onSelectKey(const event::SelectKey& e) override {
			if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
				float v = 0.f;
				if (std::sscanf(text.c_str(), "%f", &v) >= 1 && preJ) {
					*valueSrc = clamp(v, minV, maxV);
					pushModuleHistory(module, "edit step value", preJ);
					preJ = nullptr;
				}
				ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
				overlay->requestDelete();
				e.consume(this);
			}
			if (!e.getTarget())
				TextField::onSelectKey(e);
		}
		~StepValueField() {
			if (preJ) json_decref(preJ);
		}
	};


	// ---- read-only 7-segment style display (CHAN / BIG step-len / not used for BANK, see BankDisplay) ----
	struct DsegDisplay : TransparentWidget {
		AdvancedButtonSeq* module = nullptr;
		int kind = 0;// 0 = CHAN, 1 = BIG (step/len)

		void drawLayer(const DrawArgs& args, int layer) override {
			if (layer != 1) return;
			std::shared_ptr<Font> font = loadABSFont(3);
			if (!font) return;
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, box.size.y * 0.72f);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			char buf[16] = "---";
			if (module) {
				if (kind == 0) {
					snprintf(buf, sizeof(buf), "%d", module->channel + 1);
				}
				else {
					bool showStep = module->params[AdvancedButtonSeq::DISPMODE_PARAM].getValue() > 0.5f;
					snprintf(buf, sizeof(buf), "%03d", showStep ? (module->indexStep + 1) : module->length);
				}
			}
			nvgFillColor(args.vg, ABS_ACC);
			nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f + 1.f, buf, NULL);
		}
	};


	// ---- BANK A/B display: also the click target for toggling the pattern bank ----
	struct BankDisplay : OpaqueWidget {
		AdvancedButtonSeq* module = nullptr;

		void draw(const DrawArgs& args) override {
			// ShareTechMono, not DSEG7 -- the 7-segment font only covers digits, no letters
			std::shared_ptr<Font> font = loadABSFont(2);
			if (!font) return;
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, box.size.y * 0.72f);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(args.vg, ABS_ACC);
			char buf[2] = {'A', 0};
			if (module) buf[0] = module->bank[module->channel] == 0 ? 'A' : 'B';
			nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f + 1.f, buf, NULL);
		}
		void onButton(const event::Button& e) override {
			if (!module) return;
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				module->bank[module->channel] = 1 - module->bank[module->channel];
				e.consume(this);
			}
		}
	};


	// ---- real LightWidget (glow rendering shared with every LED on this panel), manually driven ----
	struct StepGateButton : OpaqueWidget {
		AdvancedButtonSeq* module = nullptr;
		int idxInPage = 0;
		app::LightWidget* led;

		StepGateButton() {
			led = new app::LightWidget();
			led->bgColor = nvgRGBA(0x33, 0x33, 0x33, 0xff);
			led->borderColor = nvgRGBA(0, 0, 0, 53);
			addChild(led);
		}
		void setLedSize(Vec ledSize) {
			led->box.size = ledSize;
			led->box.pos = box.size.div(2).minus(ledSize.div(2));
		}
		void step() override {
			OpaqueWidget::step();
			bool gateOn = false;
			if (module) {
				int s = module->page * abs_STEPS_PER_PAGE + idxInPage;
				gateOn = module->getGate(module->channel, s);
			}
			led->color = gateOn ? ABS_ACC : nvgRGBA(0, 0, 0, 0);
		}
		void onButton(const event::Button& e) override {
			if (!module) return;
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				int s = module->page * abs_STEPS_PER_PAGE + idxInPage;
				json_t* preJ = module->toJson();
				module->toggleGate(module->channel, s);
				pushModuleHistory(module, "toggle step gate", preJ);
				e.consume(this);
			}
		}
	};


	// ---- soft glow over the whole cell border when it's the currently playing step ----
	struct StepCellGlow : TransparentWidget {
		AdvancedButtonSeq* module = nullptr;
		int idxInPage = 0;
		void draw(const DrawArgs& args) override {
			if (!module) return;
			int s = module->page * abs_STEPS_PER_PAGE + idxInPage;
			if (module->indexStep != s) return;
			float w = box.size.x, h = box.size.y;
			float rr = mm2px(1.4f);
			float spread = mm2px(2.5f);
			NVGpaint glow = nvgBoxGradient(args.vg, 0, 0, w, h, rr, spread, nvgRGBA(0xff, 0x8a, 0x3c, 32), nvgRGBA(0xff, 0x8a, 0x3c, 0));
			nvgBeginPath(args.vg);
			nvgRect(args.vg, -spread, -spread, w + 2 * spread, h + 2 * spread);
			nvgFillPaint(args.vg, glow);
			nvgFill(args.vg);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.6f, 0.6f, w - 1.2f, h - 1.2f, rr);
			nvgStrokeColor(args.vg, nvgRGBA(0xff, 0x8a, 0x3c, 150));
			nvgStrokeWidth(args.vg, mm2px(0.5f));
			nvgStroke(args.vg);
		}
	};


	// ---- current-step corner indicator (no click; pure LightWidget, orange like the design) ----
	struct StepCurLight : app::LightWidget {
		AdvancedButtonSeq* module = nullptr;
		int idxInPage = 0;
		void step() override {
			bool cur = false;
			if (module) {
				int s = module->page * abs_STEPS_PER_PAGE + idxInPage;
				cur = (module->indexStep == s);
			}
			color = cur ? nvgRGB(0xff, 0x8a, 0x3c) : nvgRGBA(0, 0, 0, 0);
		}
	};


	// ---- per-step shift-left / shift-right button (tall, dark, matches the design's button style) ----
	struct StepShiftButton : OpaqueWidget {
		AdvancedButtonSeq* module = nullptr;
		int idxInPage = 0;
		int dir = 1;
		float pressFlash = 0.f;// 1 right after a click, decays to 0 -- a brief "pushed" flash

		void step() override {
			OpaqueWidget::step();
			if (pressFlash > 0.f)
				pressFlash = std::max(0.f, pressFlash - (float) APP->window->getLastFrameDuration() / 0.12f);
		}

		void draw(const DrawArgs& args) override {
			float w = box.size.x, h = box.size.y;
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.2f, 0.2f, w - 0.4f, h - 0.4f, mm2px(0.5f));
			NVGpaint grad = nvgLinearGradient(args.vg, 0, 0, 0, h, nvgRGB(0x3b, 0x3f, 0x44), nvgRGB(0x1d, 0x20, 0x23));
			nvgFillPaint(args.vg, grad);
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, nvgRGBA(0, 0, 0, 0x80));
			nvgStrokeWidth(args.vg, 0.4f);
			nvgStroke(args.vg);

			float cx = w * 0.5f, cy = h * 0.5f, r = w * 0.28f;
			nvgBeginPath(args.vg);
			if (dir > 0) { nvgMoveTo(args.vg, cx - r, cy - r * 1.3f); nvgLineTo(args.vg, cx + r, cy); nvgLineTo(args.vg, cx - r, cy + r * 1.3f); }
			else { nvgMoveTo(args.vg, cx + r, cy - r * 1.3f); nvgLineTo(args.vg, cx - r, cy); nvgLineTo(args.vg, cx + r, cy + r * 1.3f); }
			nvgClosePath(args.vg);
			nvgFillColor(args.vg, nvgRGB(0xe9, 0xeb, 0xed));
			nvgFill(args.vg);

			if (pressFlash > 0.f) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, 0.2f, 0.2f, w - 0.4f, h - 0.4f, mm2px(0.5f));
				nvgFillColor(args.vg, nvgRGBA(0, 0, 0, (unsigned char)(pressFlash * 130.f)));
				nvgFill(args.vg);
			}
		}
		void onButton(const event::Button& e) override {
			if (!module) return;
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				pressFlash = 1.f;
				int s = module->page * abs_STEPS_PER_PAGE + idxInPage;
				json_t* preJ = module->toJson();
				module->shiftStepData(module->channel, s, dir);
				pushModuleHistory(module, "shift step", preJ);
				e.consume(this);
			}
		}
	};


	// ---- per-step CV2 / CV3 / Gate-length knob: real RoundSmallBlackKnob graphics (bg + rotating
	// cap), manually composited since these bind to per-step array data, not a real ParamId.
	struct StepKnobWidget : OpaqueWidget {
		AdvancedButtonSeq* module = nullptr;
		int idxInPage = 0;
		int kind = 0;// 0 = CV2, 1 = CV3, 2 = Gate length
		json_t* dragPreJ = nullptr;
		float dragStartVal = 0.f;
		TransformWidget* tw;
		SvgWidget* fgSw;
		float lastAngle = -1000.f;
		static constexpr float minAngle = -0.75f * (float) M_PI;
		static constexpr float maxAngle = 0.75f * (float) M_PI;

		StepKnobWidget() {
			SvgWidget* bg = new SvgWidget;
			bg->setSvg(Svg::load(asset::system("res/ComponentLibrary/RoundSmallBlackKnob_bg.svg")));
			addChild(bg);

			tw = new TransformWidget;
			fgSw = new SvgWidget;
			fgSw->setSvg(Svg::load(asset::system("res/ComponentLibrary/RoundSmallBlackKnob.svg")));
			tw->box.size = fgSw->box.size;
			tw->addChild(fgSw);
			addChild(tw);

			box.size = bg->box.size;
		}

		float* valuePtr() {
			int s = module->page * abs_STEPS_PER_PAGE + idxInPage;
			int c = module->channel;
			if (kind == 0) return &module->cv2At(c, s);
			if (kind == 1) return &module->cv3At(c, s);
			return &module->glAt(c, s);
		}
		float minVal() { return kind == 2 ? 0.f : -10.f; }
		float maxVal() { return kind == 2 ? 1.f : 10.f; }

		void step() override {
			OpaqueWidget::step();
			float angle = 0.f;
			if (module) {
				float v = *valuePtr();
				float norm = clamp((v - minVal()) / (maxVal() - minVal()), 0.f, 1.f);
				angle = rescale(norm, 0.f, 1.f, minAngle, maxAngle);
			}
			if (std::abs(angle - lastAngle) > 0.0005f) {
				tw->identity();
				tw->rotate(angle, fgSw->box.size.div(2));
				lastAngle = angle;
			}
		}

		void draw(const DrawArgs& args) override {
			OpaqueWidget::draw(args);
			if (module && kind == 2 && *valuePtr() >= 0.999f) {
				float r = box.size.x * 0.5f;
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, r, r, r + 1.0f);
				nvgStrokeColor(args.vg, ABS_ACC);
				nvgStrokeWidth(args.vg, 0.7f);
				nvgStroke(args.vg);
			}
		}
		void createContextMenu() {
			ui::Menu* menu = createMenu();
			menu->addChild(createMenuLabel(kind == 0 ? "CV2 (-10V to 10V)" : (kind == 1 ? "CV3 (-10V to 10V)" : "Gate length (0 to 1, 1 = Tie/Glide)")));
			StepValueField* field = new StepValueField();
			field->box.size.x = 100;
			field->setup(module, valuePtr(), minVal(), maxVal());
			menu->addChild(field);
		}
		void onButton(const event::Button& e) override {
			if (!module) return;
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				dragPreJ = module->toJson();
				dragStartVal = *valuePtr();
				e.consume(this);
			}
			else if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				createContextMenu();
				e.consume(this);
			}
		}
		void onDragMove(const event::DragMove& e) override {
			if (!module) return;
			float range = maxVal() - minVal();
			float delta = -e.mouseDelta.y * range / 200.f;
			float* v = valuePtr();
			*v = clamp(*v + delta, minVal(), maxVal());
		}
		void onDragEnd(const event::DragEnd& e) override {
			if (dragPreJ) {
				if (module && *valuePtr() != dragStartVal)
					pushModuleHistory(module, "edit step knob", dragPreJ);
				else
					json_decref(dragPreJ);
				dragPreJ = nullptr;
			}
		}
	};


	// ---- per-step CV1 (pitch) display: right-click for numeric entry + volts/notes toggle ----
	struct Cv1DisplayWidget : TransparentWidget {
		AdvancedButtonSeq* module = nullptr;
		int idxInPage = 0;

		void drawLayer(const DrawArgs& args, int layer) override {
			if (layer != 1) return;
			std::shared_ptr<Font> font = loadABSFont(2);
			if (!font) return;
			nvgFontFaceId(args.vg, font->handle);

			nvgFontSize(args.vg, box.size.y * 0.34f);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgFillColor(args.vg, nvgRGBA(0xa6, 0xff, 0x3f, 0x60));
			nvgText(args.vg, box.size.x * 0.06f, box.size.y * 0.5f, "CV1", NULL);

			char buf[16] = "0.00";
			bool notes = true;
			if (module) {
				int s = module->page * abs_STEPS_PER_PAGE + idxInPage;
				float v = module->cv1At(module->channel, s);
				notes = module->params[AdvancedButtonSeq::NOTEDISP_PARAM].getValue() > 0.5f;
				if (notes) {
					char noteBuf[8] = {};
					printNote(v, noteBuf, true);
					snprintf(buf, sizeof(buf), "%s", noteBuf);
				}
				else {
					snprintf(buf, sizeof(buf), "%.2f", clamp(v, -10.f, 10.f));
				}
			}
			nvgFontSize(args.vg, box.size.y * 0.62f);
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			nvgFillColor(args.vg, ABS_ACC);
			nvgText(args.vg, box.size.x * 0.94f, box.size.y * 0.5f, buf, NULL);
		}

		void createContextMenu() {
			ui::Menu* menu = createMenu();
			menu->addChild(createMenuLabel("CV1 / pitch (-10V to 10V)"));
			StepValueField* field = new StepValueField();
			field->box.size.x = 100;
			int s = module->page * abs_STEPS_PER_PAGE + idxInPage;
			field->setup(module, &module->cv1At(module->channel, s), -10.f, 10.f);
			menu->addChild(field);
			menu->addChild(new MenuSeparator());
			menu->addChild(createCheckMenuItem("Show as notes", "",
				[=]() {return module->params[AdvancedButtonSeq::NOTEDISP_PARAM].getValue() > 0.5f;},
				[=]() {module->params[AdvancedButtonSeq::NOTEDISP_PARAM].setValue(1.f);}
			));
			menu->addChild(createCheckMenuItem("Show as volts", "",
				[=]() {return module->params[AdvancedButtonSeq::NOTEDISP_PARAM].getValue() <= 0.5f;},
				[=]() {module->params[AdvancedButtonSeq::NOTEDISP_PARAM].setValue(0.f);}
			));
		}

		void onButton(const event::Button& e) override {
			if (!module) return;
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && (e.mods & RACK_MOD_MASK) == 0) {
				createContextMenu();
				e.consume(this);
				return;
			}
			TransparentWidget::onButton(e);
		}
	};


	// ---- CV-cont oscilloscope: a plain rolling trace of the active channel's live CVCONT_OUTPUT --
	// always shows whatever is actually playing right now (recording pass-through, recorded-loop
	// playback, or the legacy per-step curve), with no separate caching/recording-visual logic of
	// its own -- it just watches the output, like a real scope would.
	struct CvContScopeWidget : TransparentWidget {
		AdvancedButtonSeq* module = nullptr;

		inline float yFor(float v) {
			float t = clamp((v + 10.f) / 20.f, 0.f, 1.f);// -10V..+10V mapped over the box height
			return box.size.y * (1.f - t);
		}

		void drawLayer(const DrawArgs& args, int layer) override {
			if (layer != 1) return;

			// 0V reference line
			float y0v = yFor(0.f);
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, 0.f, y0v);
			nvgLineTo(args.vg, box.size.x, y0v);
			nvgStrokeColor(args.vg, nvgRGBA(0xa6, 0xff, 0x3f, 0x30));
			nvgStrokeWidth(args.vg, 0.6f);
			nvgStroke(args.vg);

			if (!module) return;

			const int N = abs_CVCONT_SCOPE_N;
			int oldest = module->cvContScopeWriteIdx;// next slot to be overwritten = the oldest sample
			bool recording = module->cvContRecording && module->cvContRecChannel == module->channel;

			nvgBeginPath(args.vg);
			for (int i = 0; i < N; i++) {
				int idx = (oldest + i) % N;
				float x = box.size.x * ((float) i / (float)(N - 1));
				float y = yFor(module->cvContScopeBuf[idx]);
				if (i == 0) nvgMoveTo(args.vg, x, y); else nvgLineTo(args.vg, x, y);
			}
			nvgStrokeColor(args.vg, recording ? nvgRGB(0xff, 0x8a, 0x3c) : ABS_ACC);
			nvgStrokeWidth(args.vg, 1.1f);
			nvgStroke(args.vg);

			// Loop-start marker: a small ">" pinned to the left edge at the voltage the current take
			// began at (while recording) or the committed loop's first sample (otherwise) -- a fixed
			// reference so a live take can be steered back to it for a click-free loop join.
			bool haveStart = recording || !module->cvContBuf[module->channel].empty();
			if (haveStart) {
				float startVal = recording ? module->cvContRecStartValue : module->cvContBuf[module->channel].front();
				float my = yFor(startVal);
				std::shared_ptr<Font> font = loadABSFont(2);
				if (font) {
					nvgFontFaceId(args.vg, font->handle);
					nvgFontSize(args.vg, 15.f);
					nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
					nvgFillColor(args.vg, recording ? nvgRGB(0xff, 0x8a, 0x3c) : ABS_ACC);
					nvgText(args.vg, 1.f, my, ">", NULL);
				}
			}
		}
	};


	// ---- status text (page/step/bank), drawn into the PAGE SELECT header ----
	struct StatusDisplayWidget : TransparentWidget {
		AdvancedButtonSeq* module = nullptr;
		void drawLayer(const DrawArgs& args, int layer) override {
			if (layer != 1) return;
			std::shared_ptr<Font> font = loadABSFont(2);
			if (!font) return;
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, box.size.y * 0.62f);
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			char buf[64] = "CH 1 · BANK A · STEP 001 / 128";
			if (module) {
				snprintf(buf, sizeof(buf), "CH %d · BANK %c · STEP %03d / %d", module->channel + 1,
					module->bank[module->channel] == 0 ? 'A' : 'B', module->indexStep + 1, module->length);
			}
			nvgFillColor(args.vg, nvgRGB(0x63, 0x68, 0x6c));
			nvgText(args.vg, box.size.x, box.size.y * 0.5f, buf, NULL);
		}
	};


	// ---- per-step number (001..128): must reflect the active PAGE, so it can't be a static label ----
	struct StepNumberWidget : TransparentWidget {
		AdvancedButtonSeq* module = nullptr;
		int idxInPage = 0;
		void drawLayer(const DrawArgs& args, int layer) override {
			if (layer != 1) return;
			std::shared_ptr<Font> font = loadABSFont(2);
			if (!font) return;
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, box.size.y * 0.85f);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			char buf[16] = "001";
			if (module) snprintf(buf, sizeof(buf), "%03d", module->page * abs_STEPS_PER_PAGE + idxInPage + 1);
			nvgFillColor(args.vg, nvgRGB(0x63, 0x68, 0x6c));
			nvgText(args.vg, 0, box.size.y * 0.5f, buf, NULL);
		}
	};


	// ---- BefacoPush scaled up (same bg/tw/shadow resize trick as Impromptu's own IMBigPushButton) ----
	struct BigBefacoPush : BefacoPush {
		BigBefacoPush() {
			static constexpr float ratio = 2.0f;
			sw->box.size = sw->box.size.mult(ratio);
			fb->removeChild(sw);
			TransformWidget* tw = new TransformWidget();
			tw->addChild(sw);
			tw->scale(Vec(ratio, ratio));
			tw->box.size = sw->box.size;
			fb->addChild(tw);
			box.size = sw->box.size;
			if (shadow) shadow->box.size = sw->box.size;
		}
	};


	// ---- generic clickable action button (CLEAR / COPY / PASTE / SAVE-MIDI / SAVE-NATIVE / global SHIFT) ----
	struct ClickButton : OpaqueWidget {
		std::function<void()> onClick;
		std::function<void()> onRightClick;
		std::string label;
		float pressFlash = 0.f;// 1 right after a click, decays to 0 -- a brief "pushed" flash

		void step() override {
			OpaqueWidget::step();
			if (pressFlash > 0.f)
				pressFlash = std::max(0.f, pressFlash - (float) APP->window->getLastFrameDuration() / 0.12f);
		}

		// Caption is drawn by the button itself, never as a separate background label --
		// a real/opaque widget always paints over whatever sits behind it at the same spot.
		void draw(const DrawArgs& args) override {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.3f, 0.3f, box.size.x - 0.6f, box.size.y - 0.6f, mm2px(0.7f));
			NVGpaint grad = nvgLinearGradient(args.vg, 0, 0, 0, box.size.y, nvgRGB(0x3b, 0x3f, 0x44), nvgRGB(0x1d, 0x20, 0x23));
			nvgFillPaint(args.vg, grad);
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, nvgRGBA(0, 0, 0, 0x80));
			nvgStrokeWidth(args.vg, 0.4f);
			nvgStroke(args.vg);
			if (!label.empty()) {
				std::shared_ptr<Font> font = loadABSFont(0);
				if (font) {
					nvgFontFaceId(args.vg, font->handle);
					nvgFontSize(args.vg, box.size.y * 0.42f);
					nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
					nvgFillColor(args.vg, nvgRGB(0xe9, 0xeb, 0xed));
					nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, label.c_str(), NULL);
				}
			}
			if (pressFlash > 0.f) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, 0.3f, 0.3f, box.size.x - 0.6f, box.size.y - 0.6f, mm2px(0.7f));
				nvgFillColor(args.vg, nvgRGBA(0, 0, 0, (unsigned char)(pressFlash * 130.f)));
				nvgFill(args.vg);
			}
		}
		void onButton(const event::Button& e) override {
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
				pressFlash = 1.f;
				if (onClick) onClick();
				e.consume(this);
			}
			else if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				pressFlash = 1.f;
				if (onRightClick) onRightClick();
				e.consume(this);
			}
		}
	};


	// ---- clipboard helpers (JSON via the OS clipboard, for step Copy/Paste) ----
	static void copyStepsToClipboard(json_t* stepsJ, int numSteps) {
		json_t* wrapper = json_object();
		json_object_set_new(wrapper, "numSteps", json_integer(numSteps));
		json_object_set_new(wrapper, "steps", stepsJ);
		json_t* outer = json_object();
		json_object_set_new(outer, "AdvancedButtonSeq clipboard", wrapper);
		char* str = json_dumps(outer, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		json_decref(outer);
		glfwSetClipboardString(APP->window->win, str);
		free(str);
	}
	static json_t* pasteStepsFromClipboard(int* numStepsOut) {
		const char* str = glfwGetClipboardString(APP->window->win);
		if (!str) return nullptr;
		json_error_t error;
		json_t* outer = json_loads(str, 0, &error);
		if (!outer) return nullptr;
		json_t* wrapper = json_object_get(outer, "AdvancedButtonSeq clipboard");
		json_t* result = nullptr;
		if (wrapper) {
			json_t* numJ = json_object_get(wrapper, "numSteps");
			if (numJ) *numStepsOut = (int) json_integer_value(numJ);
			json_t* stepsJ = json_object_get(wrapper, "steps");
			if (stepsJ) { result = stepsJ; json_incref(result); }
		}
		json_decref(outer);
		return result;
	}


	void appendContextMenu(Menu* menu) override {
		AdvancedButtonSeq* module = static_cast<AdvancedButtonSeq*>(this->module);
		assert(module);

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Settings"));

		menu->addChild(createSubmenuItem("Retrigger gates on reset", "", [=](Menu* menu) {
			menu->addChild(createCheckMenuItem("No", "",
				[=]() {return module->retrigGatesOnReset == RGOR_NONE;},
				[=]() {module->retrigGatesOnReset = RGOR_NONE;}
			));
			menu->addChild(createCheckMenuItem("Yes", "",
				[=]() {return module->retrigGatesOnReset != RGOR_NONE;},
				[=]() {module->retrigGatesOnReset = RGOR_YES;}
			));
		}));

		menu->addChild(createBoolPtrMenuItem("Follow active step (auto page-switch)", "", &module->followPlayhead));

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Copy / Paste (whole active channel + bank, 128 steps)"));
		menu->addChild(createMenuItem("Copy channel", "", [=]() {
			copyStepsToClipboard(module->stepsToJson(module->channel, 0, abs_NUM_STEPS), abs_NUM_STEPS);
		}));
		menu->addChild(createMenuItem("Paste channel", "", [=]() {
			int n = 0;
			json_t* stepsJ = pasteStepsFromClipboard(&n);
			if (stepsJ) {
				json_t* preJ = module->toJson();
				module->stepsFromJson(module->channel, 0, stepsJ, abs_NUM_STEPS);
				json_decref(stepsJ);
				pushModuleHistory(module, "paste channel", preJ);
			}
		}));

		menu->addChild(new MenuSeparator());
		bool hasWave = !module->cvContBuf[module->channel].empty();
		menu->addChild(createMenuLabel(string::f("CV cont wave (channel %i)%s", module->channel + 1, hasWave ? "" : " -- nothing recorded")));
		menu->addChild(createMenuItem("Save wave as file...", "", [=]() {
			module->saveCvContWaveToFile(module->channel);
		}, !hasWave));
		menu->addChild(createMenuItem("Export wave as WAV...", "", [=]() {
			module->exportCvContWaveToWav(module->channel);
		}, !hasWave));
		menu->addChild(createMenuItem("Load wave from file (.wav / .abscv)...", "", [=]() {
			module->loadCvContWaveFromFile(module->channel);
		}));

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Sequence file (.abseq)"));
		menu->addChild(createMenuItem("Load sequence...", "", [=]() {
			json_t* preJ = module->toJson();
			if (module->loadNativeFromFile())
				pushModuleHistory(module, "load native sequence", preJ);
			else
				json_decref(preJ);
		}));
	}


	AdvancedButtonSeqWidget(AdvancedButtonSeq* module) {
		setModule(module);

		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/panels/AdvancedButtonSeq.svg")));
		SvgPanel* svgPanel = static_cast<SvgPanel*>(getPanel());

		PanelLabelsWidget* labelsWidget = new PanelLabelsWidget();
		labelsWidget->box.pos = VecPx(0, 0);
		labelsWidget->box.size = svgPanel->box.size;
		svgPanel->fb->addChild(labelsWidget);

		// Only the top corners have a real empty margin (the bottom margin is ~2.57mm, exactly the
		// screw's own radius, with GLOBAL CV IN jacks and the oscilloscope running right up against
		// it on both sides) -- so the bottom two screws are dropped rather than made to overlap them.
		svgPanel->fb->addChild(createWidget<ScrewSilver>(mm2px(Vec(1.29f, 1.29f))));
		svgPanel->fb->addChild(createWidget<ScrewSilver>(mm2px(Vec(PANEL_W_MM - 1.29f - 5.14f, 1.29f))));


		// ***** Column 1: MAIN CONTROLS *****

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xA_L, 16.8f)), module, AdvancedButtonSeq::RND_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xA_C, 16.8f)), module, AdvancedButtonSeq::CHAN_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xA_R, 16.8f)), module, AdvancedButtonSeq::LEN_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xA_L, 31.27f)), module, AdvancedButtonSeq::RND_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xA_C, 31.27f)), module, AdvancedButtonSeq::CHAN_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xA_R, 31.27f)), module, AdvancedButtonSeq::LEN_INPUT));
		{
			DsegDisplay* chanDisp = new DsegDisplay();
			chanDisp->box.size = mm2px(Vec(12.0f, 4.3f));
			chanDisp->box.pos = mm2px(Vec(xA_C, 24.44f)).minus(chanDisp->box.size.div(2));// matches disp(xA_C-6, 22.29, 12, 4.3); jack at 31.27 intentionally untouched
			chanDisp->module = module; chanDisp->kind = 0;
			addChild(chanDisp);
		}

		for (int i = 0; i < abs_NUM_CHANNELS; i++) {
			float x = xA_L + i * ((xA_R - xA_L) / (abs_NUM_CHANNELS - 1));
			addChild(createLightCentered<SmallLight<OrangeLightIM>>(mm2px(Vec(x, chled_y0 + 1.2f)), module, AdvancedButtonSeq::CHAN_LIGHT + i));
		}

		{
			DsegDisplay* bigDisp = new DsegDisplay();
			bigDisp->box.size = mm2px(Vec(17.0f, 6.0f));
			bigDisp->box.pos = mm2px(Vec(xA_L, 45.274f)).minus(bigDisp->box.size.div(2));// matches disp(xA_L-8.5, bigbank_y0, 17, 6.0)
			bigDisp->module = module; bigDisp->kind = 1;
			addChild(bigDisp);
			addParam(createParamCentered<CKSS>(mm2px(Vec(xA_L, 52.2f)), module, AdvancedButtonSeq::DISPMODE_PARAM));
		}
		{
			BankDisplay* bankDisp = new BankDisplay();
			bankDisp->box.size = mm2px(Vec(14.0f, 4.2f));
			bankDisp->box.pos = mm2px(Vec(xA_C, 45.424f)).minus(bankDisp->box.size.div(2));// matches the dark box drawn in the SVG (bigbank_y0+1.0 + h/2)
			bankDisp->module = module;
			addChild(bankDisp);
			addChild(createLightCentered<SmallLight<OrangeLightIM>>(mm2px(Vec(xA_C - 3.0f, 50.9f)), module, AdvancedButtonSeq::BANK_LIGHT + 0));
			addChild(createLightCentered<SmallLight<OrangeLightIM>>(mm2px(Vec(xA_C + 3.0f, 50.9f)), module, AdvancedButtonSeq::BANK_LIGHT + 1));
		}
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xA_R, 46.27f)), module, AdvancedButtonSeq::BANK_INPUT));

		addParam(createParamCentered<TL1105>(mm2px(Vec(xA_L, 62.6f)), module, AdvancedButtonSeq::CLOCK_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xA_L, 69.83f)), module, AdvancedButtonSeq::CLK_INPUT));
		addParam(createParamCentered<TL1105>(mm2px(Vec(xA_C, 62.6f)), module, AdvancedButtonSeq::RESET_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xA_C, 69.83f)), module, AdvancedButtonSeq::RESET_INPUT));
		addParam(createParamCentered<TL1105>(mm2px(Vec(xA_R, 62.6f)), module, AdvancedButtonSeq::DEL_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xA_R, 69.83f)), module, AdvancedButtonSeq::DEL_INPUT));

		addParam(createParamCentered<TL1105>(mm2px(Vec(xA_L, 80.67f)), module, AdvancedButtonSeq::FILL_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xA_L, 87.9f)), module, AdvancedButtonSeq::FILL_INPUT));
		addParam(createParamCentered<TL1105>(mm2px(Vec(xA_C, 80.67f)), module, AdvancedButtonSeq::WRITEFILL_PARAM));
		addChild(createLightCentered<MediumLight<GreenLightIM>>(mm2px(Vec(xA_C, 87.9f)), module, AdvancedButtonSeq::WRITEFILL_LIGHT));
		addParam(createParamCentered<TL1105>(mm2px(Vec(xA_R, 80.67f)), module, AdvancedButtonSeq::QUANTIZEBIG_PARAM));
		addChild(createLightCentered<MediumLight<GreenLightIM>>(mm2px(Vec(xA_R, 87.9f)), module, AdvancedButtonSeq::QUANTIZEBIG_LIGHT));

		{
			ClickButton* clearBtn = new ClickButton();
			clearBtn->label = "CLEAR";
			clearBtn->box.size = mm2px(Vec(11.0f, 4.6f));
			clearBtn->box.pos = mm2px(Vec(xA_L, 95.84f)).minus(clearBtn->box.size.div(2));
			AdvancedButtonSeq* m = module;
			clearBtn->onClick = [m]() {
				if (!m) return;
				json_t* preJ = m->toJson();
				m->clearChannel(m->channel);
				pushModuleHistory(m, "clear channel", preJ);
			};
			addChild(clearBtn);
		}
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xA_L, 103.0f)), module, AdvancedButtonSeq::CLEAR_INPUT));
		addParam(createParamCentered<BigBefacoPush>(mm2px(Vec(xA_C, 99.77f)), module, AdvancedButtonSeq::BIG_PARAM));
		// x aligned with the CV cont jack's column below (cvin_xs[3]), same as GATE_LEN_MODE_PARAM's
		// switch lines up with the CV cont sampler switch below it.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cvin_xs[3], 99.77f)), module, AdvancedButtonSeq::BIG_INPUT));
		addParam(createParamCentered<CKSS>(mm2px(Vec(cvcont_sw_x, 99.77f)), module, AdvancedButtonSeq::GATE_LEN_MODE_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cvin_xs[0], 119.6f)), module, AdvancedButtonSeq::CV1_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cvin_xs[1], 119.6f)), module, AdvancedButtonSeq::CV2_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cvin_xs[2], 119.6f)), module, AdvancedButtonSeq::CV3_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cvin_xs[3], 119.6f)), module, AdvancedButtonSeq::CVCONT_INPUT));
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(cvcont_sw_x, cvcont_sw_y)), module, AdvancedButtonSeq::CVCONT_MODE_PARAM));


		// ***** Column 2: STEPS *****

		{
			StatusDisplayWidget* status = new StatusDisplayWidget();
			status->box.size = mm2px(Vec(90.0f, 3.5f));
			status->box.pos = mm2px(Vec(col2_content_x1 - 90.0f, c2_hdr_y1 - 3.0f));
			status->module = module;
			addChild(status);
		}

		for (int i = 0; i < abs_NUM_PAGES; i++) {
			addParam(createParamCentered<LEDButton>(mm2px(Vec(page_xs[i], PAGE_BTN_Y)), module, AdvancedButtonSeq::PAGE_PARAM + i));
			addChild(createLightCentered<MediumLight<GreenLightIM>>(mm2px(Vec(page_xs[i], PAGE_BTN_Y)), module, AdvancedButtonSeq::PAGE_LIGHT + i));
		}

		for (int idx = 0; idx < abs_STEPS_PER_PAGE; idx++) {
			int col = idx % 4;
			int row = idx / 4;
			float cx = gridX0 + col * (cellW + GAP_CELL);
			float cy = gridY0 + row * (cellH + GAP_CELL);

			StepCellGlow* cellGlow = new StepCellGlow();
			cellGlow->box.pos = mm2px(Vec(cx, cy));
			cellGlow->box.size = mm2px(Vec(cellW, cellH));
			cellGlow->module = module; cellGlow->idxInPage = idx;
			addChild(cellGlow);

			StepNumberWidget* stepNum = new StepNumberWidget();
			stepNum->box.pos = mm2px(Vec(cx + 2.0f, cy + CELL_NUM_Y - 1.5f));
			stepNum->box.size = mm2px(Vec(12.0f, 3.0f));
			stepNum->module = module; stepNum->idxInPage = idx;
			addChild(stepNum);

			StepCurLight* curLed = new StepCurLight();
			curLed->box.size = mm2px(Vec(2.4f, 2.4f));
			curLed->box.pos = mm2px(Vec(cx + cellW - 4.0f, cy + CELL_NUM_Y)).minus(curLed->box.size.div(2));
			curLed->module = module; curLed->idxInPage = idx;
			addChild(curLed);

			StepShiftButton* shiftL = new StepShiftButton();
			shiftL->box.pos = mm2px(Vec(cx + 1.0f, cy + CELL_SHIFT_Y0));
			shiftL->box.size = mm2px(Vec(CELL_SHIFT_W, CELL_SHIFT_H));
			shiftL->module = module; shiftL->idxInPage = idx; shiftL->dir = -1;
			addChild(shiftL);

			StepShiftButton* shiftR = new StepShiftButton();
			shiftR->box.pos = mm2px(Vec(cx + cellW - 1.0f - CELL_SHIFT_W, cy + CELL_SHIFT_Y0));
			shiftR->box.size = mm2px(Vec(CELL_SHIFT_W, CELL_SHIFT_H));
			shiftR->module = module; shiftR->idxInPage = idx; shiftR->dir = 1;
			addChild(shiftR);

			const float knobXs[3] = {cx + cellW / 2 - CELL_KNOB_PITCH, cx + cellW / 2, cx + cellW / 2 + CELL_KNOB_PITCH};
			for (int k = 0; k < 3; k++) {
				StepKnobWidget* knob = new StepKnobWidget();// box.size set internally from the real Trimpot-family SVG
				knob->box.pos = mm2px(Vec(knobXs[k], cy + CELL_KNOB_Y)).minus(knob->box.size.div(2));
				knob->module = module; knob->idxInPage = idx; knob->kind = k;
				addChild(knob);
			}

			StepGateButton* gateBtn = new StepGateButton();
			gateBtn->box.size = mm2px(Vec(4.6f, CELL_CV1_H));
			gateBtn->box.pos = mm2px(Vec(cx + cellW - 1.0f - 2.3f, cy + CELL_CV1_Y)).minus(gateBtn->box.size.div(2));
			gateBtn->setLedSize(mm2px(Vec(3.05f, 3.05f)));
			gateBtn->module = module; gateBtn->idxInPage = idx;
			addChild(gateBtn);

			Cv1DisplayWidget* cv1Disp = new Cv1DisplayWidget();
			// must match the dark box drawn in the SVG exactly (disp(cx+5.5, cy+CELL_CV1_Y-h/2, cellW-13, h))
			// or the dim "CV1" tag drawn near the widget's own left edge falls outside the dark screen
			cv1Disp->box.pos = mm2px(Vec(cx + 5.5f, cy + CELL_CV1_Y - CELL_CV1_H * 0.5f));
			cv1Disp->box.size = mm2px(Vec(cellW - 13.0f, CELL_CV1_H));
			cv1Disp->module = module; cv1Disp->idxInPage = idx;
			addChild(cv1Disp);
		}

		AdvancedButtonSeq* m = module;
		{
			ClickButton* copyBtn = new ClickButton();
			copyBtn->label = "COPY";
			copyBtn->box.pos = mm2px(Vec(action_xs[0], c2_action_y0));
			copyBtn->box.size = mm2px(Vec(ACTION_W_MM, ACTION_H_MM));
			copyBtn->onClick = [m]() {
				if (!m) return;
				copyStepsToClipboard(m->stepsToJson(m->channel, m->page * abs_STEPS_PER_PAGE, abs_STEPS_PER_PAGE), abs_STEPS_PER_PAGE);
			};
			addChild(copyBtn);

			ClickButton* pasteBtn = new ClickButton();
			pasteBtn->label = "PASTE";
			pasteBtn->box.pos = mm2px(Vec(action_xs[1], c2_action_y0));
			pasteBtn->box.size = mm2px(Vec(ACTION_W_MM, ACTION_H_MM));
			pasteBtn->onClick = [m]() {
				if (!m) return;
				int n = 0;
				json_t* stepsJ = pasteStepsFromClipboard(&n);
				if (stepsJ) {
					json_t* preJ = m->toJson();
					m->stepsFromJson(m->channel, m->page * abs_STEPS_PER_PAGE, stepsJ, abs_STEPS_PER_PAGE);
					json_decref(stepsJ);
					pushModuleHistory(m, "paste page", preJ);
				}
			};
			addChild(pasteBtn);

			ClickButton* midiBtn = new ClickButton();
			midiBtn->label = "SAVE MIDI";
			midiBtn->box.pos = mm2px(Vec(action_xs[2], c2_action_y0));
			midiBtn->box.size = mm2px(Vec(ACTION_W_MM, ACTION_H_MM));
			midiBtn->onClick = [m]() { if (m) m->saveMidiToFile(); };
			addChild(midiBtn);

			ClickButton* nativeBtn = new ClickButton();
			nativeBtn->label = "SAVE NATIVE*";
			nativeBtn->box.pos = mm2px(Vec(action_xs[3], c2_action_y0));
			nativeBtn->box.size = mm2px(Vec(ACTION_W_MM, ACTION_H_MM));
			nativeBtn->onClick = [m]() { if (m) m->saveNativeToFile(); };
			nativeBtn->onRightClick = [m]() {
				if (!m) return;
				json_t* preJ = m->toJson();
				if (m->loadNativeFromFile())
					pushModuleHistory(m, "load native sequence", preJ);
				else
					json_decref(preJ);
			};
			addChild(nativeBtn);

			ClickButton* shiftLBtn = new ClickButton();
			shiftLBtn->label = "<<< SHIFT";
			shiftLBtn->box.pos = mm2px(Vec(action_xs[4], c2_action_y0));
			shiftLBtn->box.size = mm2px(Vec(ACTION_W_MM, ACTION_H_MM));
			shiftLBtn->onClick = [m]() {
				if (!m) return;
				json_t* preJ = m->toJson();
				m->globalShift(m->channel, -1);
				pushModuleHistory(m, "shift sequence left", preJ);
			};
			addChild(shiftLBtn);

			ClickButton* shiftRBtn = new ClickButton();
			shiftRBtn->label = "SHIFT >>>";
			shiftRBtn->box.pos = mm2px(Vec(action_xs[5], c2_action_y0));
			shiftRBtn->box.size = mm2px(Vec(ACTION_W_MM, ACTION_H_MM));
			shiftRBtn->onClick = [m]() {
				if (!m) return;
				json_t* preJ = m->toJson();
				m->globalShift(m->channel, 1);
				pushModuleHistory(m, "shift sequence right", preJ);
			};
			addChild(shiftRBtn);
		}


		// ***** Column 3: OUTPUTS MATRIX *****

		for (int c = 0; c < abs_NUM_CHANNELS; c++) {
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(chan_xs[c], matrix_row_y[0])), module, AdvancedButtonSeq::CV1_OUTPUT + c));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(chan_xs[c], matrix_row_y[1])), module, AdvancedButtonSeq::CV2_OUTPUT + c));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(chan_xs[c], matrix_row_y[2])), module, AdvancedButtonSeq::CV3_OUTPUT + c));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(chan_xs[c], matrix_row_y[3])), module, AdvancedButtonSeq::CVCONT_OUTPUT + c));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(chan_xs[c], matrix_row_y[4])), module, AdvancedButtonSeq::GATE_OUTPUT + c));
		}
		for (int j = 0; j < 3; j++) {
			addParam(createParamCentered<LEDButton>(mm2px(Vec(sh_x, matrix_row_y[j])), module, AdvancedButtonSeq::SAMPLEHOLD_PARAM + j));
			addChild(createLightCentered<MediumLight<GreenLightIM>>(mm2px(Vec(sh_x, matrix_row_y[j])), module, AdvancedButtonSeq::SAMPLEHOLD_LIGHT + j));
		}

		// EoC out shares CH6's column (axis matches chan_xs[5]); the old chan_xs[0] spot now holds
		// the oscilloscope speed knob instead.
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(chan_xs[5], eoc_y)), module, AdvancedButtonSeq::EOC_OUTPUT));
		addChild(createLightCentered<MediumLight<RedLightIM>>(mm2px(Vec(chan_xs[5] + 6.0f, eoc_y)), module, AdvancedButtonSeq::EOC_LIGHT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(chan_xs[0], eoc_y)), module, AdvancedButtonSeq::SCOPE_SPEED_PARAM));

		{
			CvContScopeWidget* scope = new CvContScopeWidget();
			// must match the dark box drawn in the SVG exactly (disp(col3_content_x0, infobox_y0, ...))
			scope->box.pos = mm2px(Vec(col3_content_x0, infobox_y0));
			scope->box.size = mm2px(Vec(col3_content_x1 - col3_content_x0, infobox_y1 - infobox_y0));
			scope->module = module;
			addChild(scope);
		}
	}
};


Model* modelAdvancedButtonSeq = createModel<AdvancedButtonSeq, AdvancedButtonSeqWidget>("AdvancedButtonSeq");
