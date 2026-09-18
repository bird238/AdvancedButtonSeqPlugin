//***********************************************************************************************
//Shared engine/GUI utilities used by AdvancedButtonSeq.
//Originates from Marc Boulé's Impromptu Modular; trimmed to only what this module needs.
//See ./LICENSE.md for all licenses
//***********************************************************************************************

#include "ImpromptuModular.hpp"


Plugin *pluginInstance;


void init(Plugin *p) {
	pluginInstance = p;
	readThemeAndContrastFromDefault();
	p->addModel(modelAdvancedButtonSeq);
}


static const char noteLettersSharp[12] = {'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B'};
static const char noteLettersFlat [12] = {'C', 'D', 'D', 'E', 'E', 'F', 'G', 'G', 'A', 'A', 'B', 'B'};
static const char isBlackKey      [12] = { 0,   1,   0,   1,   0,   0,   1,   0,   1,   0,   1,   0 };

int printNote(float cvVal, char* text, bool sharp) {// text needs at least 4 chars (3 displayed + terminator)
	int indexNote, octave;
	calcNoteAndOct(cvVal, &indexNote, &octave);

	text[0] = sharp ? noteLettersSharp[indexNote] : noteLettersFlat[indexNote];
	int cursor = 1;

	if (isBlackKey[indexNote] == 1) {
		text[cursor] = (sharp ? '\"' : 'b');
		cursor++;
	}

	octave += 4;
	if (octave >= 0 && octave <= 9) {
		text[cursor] = (char) (0x30 + octave);
		cursor++;
	}

	text[cursor] = 0;
	return cursor;
}
