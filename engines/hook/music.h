/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef HOOK_MUSIC_H
#define HOOK_MUSIC_H

#include "common/array.h"
#include "common/mutex.h"

#include "hook/resources.h"

namespace OPL {
class OPL;
}
class MidiDriver;

namespace Hook {

/**
 * The game's sound drivers, ADLIB.BIN and ROLAND.BIN, carry their own sequencer and their own
 * arrangement of the score (docs/formats.md section 5, "Sound"). Both run the same stream
 * format; this class is a port of their timer tick (+0), driven at the original rate of
 * 1193182 / 0x58EC = 52.41 Hz. The engine talks to it through the driver's request bytes
 * (+8 song, +9 song playing, +0A effect, +0E effect volume), as the original did.
 *
 * The music data stays in the driver file: every address below is an offset into it.
 */
class MusicDriver {
public:
	enum Device { kAdLib, kMT32, kGM };

	struct Layout {
		const char *file;
		uint16 songs, effects, instruments, voices;
		uint8 numVoices;		///< voice records
		uint8 effectTop;		///< effects search for a free voice from here down to 0
		uint8 songLimit;		///< requests >= this stop the music
		uint8 effectLimit;		///< effect requests >= this are ignored
		uint8 gateOffset;		///< instrument byte with the early-release gate
		uint8 speedOffset;		///< header byte: tempo countdown
	};

	virtual ~MusicDriver() {}

	/** Pick the device from the ScummVM settings; nullptr = no music. */
	static MusicDriver *create(const Resources &res);
	/** Without output, for the test harness: every register write / MIDI message is logged. */
	static MusicDriver *createLogger(const Resources &res, Device device);

	/** Engine 00238: request song n (1..7/8; above = stop) unless it is already playing. */
	void playSong(uint8 n);
	/** The effect request byte [+0A] and, if volume >= 0, the effect volume [+0E]. */
	void requestEffect(uint8 n, int volume);
	/** Silence everything at shutdown. */
	virtual void stop() {}
	/** Re-read the volume settings. */
	virtual void syncVolume() {}

	/** One driver tick (also called directly by the test harness). */
	void tick();
	/** Logged output, one line per write: "tick reg value" or "tick status data...". */
	const Common::Array<Common::String> &log() const { return _log; }
	uint32 ticks() const { return _t; }

protected:
	struct Voice {
		uint8 note = 0, count = 0, state = 0, dur = 0, transpose = 0, slide = 0;
		uint16 inst = 0;			///< instrument record (an offset into the driver)
		uint8 instNo = 0;			///< Roland: last instrument number
		uint8 level = 0;			///< AdLib carrier level / Roland volume
		uint8 modLevel = 0, fxVolume = 0;
		uint8 b0 = 0;				///< AdLib: B0 value without key-on
		uint8 mod = 0, car = 0, ch = 0;
		uint16 order = 0, ptr = 0;
		uint16 loops = 0;
	};

	MusicDriver(const Layout &layout) : _layout(layout) {}
	bool load(const Resources &res);

	/** A byte of the driver. Voices start with instrument pointer 0, so instrument reads can
	 *  land on the header, whose request bytes are live state. */
	uint8 u8(uint32 o) const;
	uint16 u16(uint32 o) const { return u8(o) | (u8(o + 1) << 8); }

	// Device output, called with the mutex held
	virtual void songStarted() = 0;
	virtual void noteStart(Voice &v, uint8 note) = 0;	///< stream byte 00..7F
	virtual void keyOff(Voice &v) = 0;					///< rest / end / early release
	virtual void slideStep(Voice &v) = 0;				///< 86/87: note +- 1 every step
	virtual void setInstrument(Voice &v, uint8 k) = 0;
	virtual void setLevel(Voice &v, uint8 level) = 0;	///< 9F
	virtual void allOff() = 0;							///< stop the song

	void logLine(const Common::String &s);

	const Layout &_layout;
	Buffer _drv;
	Common::Array<Voice> _v;
	Common::Mutex _mutex;
	bool _logging = false;
	Common::Array<Common::String> _log;
	uint32 _t = 0;
	uint8 _songVoices = 0;

private:
	void startSong(uint8 n);
	void stopSong();
	bool startEffect(uint8 n);
	void read(Voice &v);
	void step(Voice &v);

	uint8 _songRequest = 0;		///< [+8]
	uint8 _playing = 0;			///< [+9]
	uint8 _effectRequest = 0;	///< [+0A]
	uint8 _effectVolume = 0;	///< [+0E]
	uint8 _speed = 1, _tempo = 1;
};

/** ADLIB.BIN on an emulated OPL2. */
class AdLibMusic : public MusicDriver {
public:
	AdLibMusic(bool output);
	~AdLibMusic() override;
	bool init(const Resources &res);
	void stop() override;
	void syncVolume() override;

protected:
	void songStarted() override;
	void noteStart(Voice &v, uint8 note) override;
	void keyOff(Voice &v) override;
	void slideStep(Voice &v) override;
	void setInstrument(Voice &v, uint8 k) override;
	void setLevel(Voice &v, uint8 level) override;
	void allOff() override;

private:
	void write(uint8 reg, uint8 val);
	void levels(Voice &v);
	void keyOn(Voice &v);
	void onTimer();

	bool _output;
	OPL::OPL *_opl = nullptr;
	uint32 _clock = 0;
	int _volume = 256;
	uint8 _carrierOp[0x20];		///< 1 if register 40+op is a carrier level
};

/** ROLAND.BIN on an MT-32/LAPC-I, or mapped to General MIDI. */
class RolandMusic : public MusicDriver {
public:
	RolandMusic(MidiDriver *midi, bool gm);
	~RolandMusic() override;
	bool init(const Resources &res);
	void stop() override;
	void syncVolume() override;

protected:
	void songStarted() override;
	void noteStart(Voice &v, uint8 note) override;
	void keyOff(Voice &v) override;
	void slideStep(Voice &v) override;
	void setInstrument(Voice &v, uint8 k) override;
	void setLevel(Voice &v, uint8 level) override;
	void allOff() override;

private:
	uint8 channel(const Voice &v) const;
	void noteMsg(const Voice &v, uint8 status);
	void send(uint8 status, uint8 a, int b = -1);
	static void timerProc(void *param);

	MidiDriver *_midi;
	bool _gm;
	uint64 _clock = 0;
	int _volume = 256;
};

} // End of namespace Hook

#endif
