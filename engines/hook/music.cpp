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

#include "audio/fmopl.h"
#include "audio/mididrv.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/func.h"
#include "common/textconsole.h"

#include "hook/music.h"

namespace Hook {

// The engine's timer interrupt adds its own PIT period on every call and enters the driver
// each time the sum passes 0x58EC counts (02FB5), so the driver runs at 1193182 / 0x58EC Hz
// whatever the frame rate.
static const uint32 kPitHz = 1193182;
static const uint32 kPitPerTick = 0x58EC;

static const MusicDriver::Layout kAdLibLayout = {
	"ADLIB.BIN", 0x0BC3, 0x22A4, 0x0862, 0x04F0, 9, 8, 8, 0x1E, 0x0E, 0x0C
};
static const MusicDriver::Layout kRolandLayout = {
	"ROLAND.BIN", 0x0966, 0x21D5, 0x05D6, 0x0513, 8, 6, 9, 0x20, 0x09, 0x0D
};

// ---------------------------------------------------------------------------------------------
// Sequencer, common to both drivers (AdLib 0046 / Roland 0047)

bool MusicDriver::load(const Resources &res) {
	if (!res.load(_layout.file, _drv) || _drv.size() < 0x20)
		return false;
	_v.resize(_layout.numVoices);
	for (uint k = 0; k < _v.size(); k++) {
		uint16 r = _layout.voices + 0x15 * k;
		_v[k].inst = u16(r + 2);
		_v[k].mod = u8(r + 5);
		_v[k].car = u8(r + 6);
		_v[k].ch = u8(r + 7);
		_v[k].b0 = u8(r + 9);
	}
	_effectVolume = u8(0x0E);
	return true;
}

uint8 MusicDriver::u8(uint32 o) const {
	if (o == 0x08)
		return _songRequest;
	if (o == 0x09)
		return _playing;
	if (o == 0x0A)
		return _effectRequest;
	if (o == 0x0E)
		return _effectVolume;
	if (o == _layout.speedOffset)
		return _speed;
	if (o == (_layout.speedOffset == 0x0D ? 0x0Cu : 0x0BC2u))
		return _tempo;
	return o < _drv.size() ? _drv[o] : 0;
}

void MusicDriver::logLine(const Common::String &s) {
	_log.push_back(Common::String::format("%u ", _t) + s);
}

void MusicDriver::playSong(uint8 n) {
	// 00238: only when it differs from the song playing; writing the word [+8] also clears
	// [+9], which lets the driver take the request (the original then waits 2 timer ticks)
	Common::StackLock lock(_mutex);
	if (n == _playing)
		return;
	_songRequest = n;
	_playing = 0;
}

void MusicDriver::requestEffect(uint8 n, int volume) {
	Common::StackLock lock(_mutex);
	_effectRequest = n;
	if (volume >= 0)
		_effectVolume = (uint8)volume;
}

void MusicDriver::tick() {
	Common::StackLock lock(_mutex);
	// AdLib 0373 / Roland 02BE: a song request is only taken while nothing is playing
	if (!_playing && _songRequest) {
		uint8 n = _songRequest;
		_songRequest = 0;
		if (n >= _layout.songLimit)
			stopSong();
		else
			startSong(n);
	}
	// AdLib 0420 / Roland 03A7: an effect waits until a voice is free
	if (_effectRequest && _effectRequest < _layout.effectLimit)
		startEffect(_effectRequest);

	_speed--;
	for (Voice &v : _v)
		step(v);
	if (_speed == 0)
		_speed = _tempo;
	_t++;
}

void MusicDriver::startSong(uint8 n) {
	uint16 p = u16(_layout.songs + 2 * n);
	_playing = n;
	for (Voice &v : _v)
		v.state = 0;
	_tempo = u8(p);
	_songVoices = MIN<uint8>(u8(p + 1), _v.size());
	for (uint k = 0; k < _songVoices; k++) {
		Voice &v = _v[k];
		uint16 a = u16(p + 2 + 2 * k);
		v.ptr = u16(a);
		v.order = a + 2;
		v.count = 1;
		v.state = 1;
		v.loops = 0;
	}
	songStarted();
	_speed = 1;
}

void MusicDriver::stopSong() {
	// AdLib 02E4 / Roland 021D
	allOff();
	for (uint k = 0; k < _songVoices; k++)
		_v[k].state = 0;
	_songRequest = 0;
	_playing = 0;
}

bool MusicDriver::startEffect(uint8 n) {
	for (int k = _layout.effectTop; k >= 0; k--) {
		Voice &v = _v[k];
		if (v.state)
			continue;
		_effectRequest = 0;		// cleared before the stream is read (043F / 03D1)
		v.state = 2;
		v.ptr = u16(_layout.effects + 2 * n);
		v.slide = 0;
		v.fxVolume = _effectVolume;
		read(v);
		return true;
	}
	return false;
}

void MusicDriver::read(Voice &v) {
	for (int guard = 0; guard < 4096; guard++) {
		uint8 c = u8(v.ptr++);
		if (c < 0x80) {
			noteStart(v, c);
			v.count = v.dur;
			return;
		}
		if (c >= 0xE0) {
			v.dur = c - 0xDF;
		} else if (c >= 0xA0) {
			setInstrument(v, c - 0xA0);
		} else if (c == 0x9F) {
			setLevel(v, u8(v.ptr++));
		} else if (c == 0x80) {
			// next pattern; 0 ends the order list, followed by the loop address
			uint16 p = u16(v.order);
			v.order += 2;
			if (!p) {
				v.order = u16(v.order);
				p = u16(v.order);
				v.order += 2;
				v.loops++;
			}
			v.ptr = p;
		} else if (c == 0x81) {
			keyOff(v);
			v.count = v.dur;
			return;
		} else if (c == 0x82) {
			v.count = v.dur;
			return;
		} else if (c == 0x83) {
			stopSong();
			return;
		} else if (c == 0x84) {
			v.transpose = u8(v.ptr++);
		} else if (c == 0x85) {
			keyOff(v);
			v.state = 0;
			return;
		} else if (c == 0x86 || c == 0x87) {
			v.slide = (c == 0x86) ? 1 : 0xFF;
		} else {
			warning("Hook: bad music stream byte %02X at %04X", c, v.ptr - 1);
			v.state = 0;
			return;
		}
	}
	v.state = 0;
}

void MusicDriver::step(Voice &v) {
	// AdLib 00A8 / Roland 00A6: music voices advance when the tempo countdown is 0,
	// effect voices on every tick
	if (!v.state)
		return;
	if (v.state != 2 && _speed)
		return;
	if (--v.count == 0) {
		read(v);
		return;
	}
	if (v.slide) {
		v.note += v.slide;
		slideStep(v);
	}
	// early release when the instrument's gate is reached (not before a tie)
	if (u8(v.ptr) != 0x82 && u8(v.inst + _layout.gateOffset) == v.count) {
		keyOff(v);
		v.slide = 0;
	}
}

// ---------------------------------------------------------------------------------------------
// AdLib (ADLIB.BIN)

static const uint16 kAdLibOctave = 0x07B1, kAdLibSemitone = 0x0811, kAdLibFnum = 0x0631;
static const uint16 kAdLibAttenuation = 0x05B1;

AdLibMusic::AdLibMusic(bool output) : MusicDriver(kAdLibLayout), _output(output) {
	memset(_carrierOp, 0, sizeof(_carrierOp));
}

AdLibMusic::~AdLibMusic() {
	stop();
	delete _opl;
}

bool AdLibMusic::init(const Resources &res) {
	if (!load(res))
		return false;
	for (const Voice &v : _v)
		if (v.car < sizeof(_carrierOp))
			_carrierOp[v.car] = 1;
	if (!_output) {
		_logging = true;
		return true;
	}
	_opl = OPL::Config::create();
	if (!_opl || !_opl->init()) {
		delete _opl;
		_opl = nullptr;
		return false;
	}
	syncVolume();
	_opl->start(new Common::Functor0Mem<void, AdLibMusic>(this, &AdLibMusic::onTimer));
	return true;
}

void AdLibMusic::onTimer() {
	// called at OPL::OPL::kDefaultCallbackFrequency (250 Hz)
	_clock += kPitHz;
	while (_clock >= kPitPerTick * (uint32)OPL::OPL::kDefaultCallbackFrequency) {
		_clock -= kPitPerTick * OPL::OPL::kDefaultCallbackFrequency;
		tick();
	}
}

void AdLibMusic::stop() {
	if (_opl) {
		_opl->stop();
		for (int ch = 0; ch < 9; ch++)
			_opl->writeReg(0xB0 + ch, 0);
	}
}

void AdLibMusic::syncVolume() {
	// The OPL stream is mixed as a plain sound: apply the music volume to the carrier levels
	bool mute = ConfMan.hasKey("mute") && ConfMan.getBool("mute");
	_volume = mute ? 0 : CLIP(ConfMan.getInt("music_volume"), 0, 256);
}

void AdLibMusic::write(uint8 reg, uint8 val) {
	if (_logging)
		logLine(Common::String::format("%02X %02X", reg, val));
	if (!_opl)
		return;
	if (reg >= 0x40 && reg < 0x60 && _carrierOp[reg - 0x40] && _volume < 256) {
		int att = val & 0x3F;
		att = 63 - (63 - att) * _volume / 256;
		val = (val & 0xC0) | att;
	}
	_opl->writeReg(reg, val);
}

void AdLibMusic::songStarted() {
	// 03F1: all channels off, then waveform select, deep tremolo/vibrato, timers
	for (const Voice &v : _v)
		write(0xB0 + v.ch, v.b0);
	write(0x01, 0x20);
	write(0xBD, 0xC0);
	write(0x08, 0x00);
	write(0x04, 0x21);
}

void AdLibMusic::levels(Voice &v) {
	// levels 0..7F -> attenuation [05B1 + v]; KSL from instrument +0C
	uint8 ksl = u8(v.inst + 0x0C);
	write(0x40 + v.car, u8(kAdLibAttenuation + (v.level & 0x7F)) | ((ksl << 2) & 0xC0));
	write(0x40 + v.mod, u8(kAdLibAttenuation + (v.modLevel & 0x7F)) | (((ksl >> 2) | (ksl << 6)) & 0xC0));
}

void AdLibMusic::keyOn(Voice &v) {
	// 028B: octave [07B1 + n], F-number [0631 + 32 * semitone [0811 + n]]
	uint8 n = v.note + v.transpose;
	int bl = u8(kAdLibOctave + n) - 1;
	int f = (int16)u16(kAdLibFnum + 32 * u8(kAdLibSemitone + n));
	if (f < 0)
		bl++;
	if (bl < 0) {
		bl++;
		f >>= 1;
	}
	f &= 0xFFFF;
	write(0xA0 + v.ch, f & 0xFF);
	v.b0 = ((f >> 8) & 3) | ((bl << 2) & 0xFF);
	write(0xB0 + v.ch, v.b0 | 0x20);
}

void AdLibMusic::noteStart(Voice &v, uint8 note) {
	v.note = note;
	if (v.state == 2) {
		v.level = v.fxVolume;
		levels(v);
	}
	keyOff(v);
	keyOn(v);
}

void AdLibMusic::keyOff(Voice &v) {
	write(0xB0 + v.ch, v.b0);
}

void AdLibMusic::slideStep(Voice &v) {
	keyOn(v);
}

void AdLibMusic::setInstrument(Voice &v, uint8 k) {
	// 01B6
	uint16 s = kAdLibLayout.instruments + 16 * k;
	v.inst = s;
	write(0x60 + v.mod, u8(s + 0));
	write(0x60 + v.car, u8(s + 1));
	write(0x80 + v.mod, u8(s + 2));
	write(0x80 + v.car, u8(s + 3));
	write(0xE0 + v.mod, u8(s + 6));
	write(0xE0 + v.car, u8(s + 7));
	write(0xC0 + v.ch, u8(s + 9));
	write(0x20 + v.mod, u8(s + 4));
	write(0x20 + v.car, u8(s + 5));
	v.level = u8(s + 0x0A);
	if (v.state == 2)
		v.fxVolume = v.level;
	v.modLevel = u8(s + 0x0B);
	levels(v);
	v.transpose = u8(s + 8);
}

void AdLibMusic::setLevel(Voice &v, uint8 level) {
	v.level = level;
	if (v.state == 2)
		v.fxVolume = level;
	levels(v);
}

void AdLibMusic::allOff() {
	// 02E4: key off the song's voices
	for (uint k = 0; k < _songVoices; k++)
		keyOff(_v[k]);
}

// ---------------------------------------------------------------------------------------------
// Roland (ROLAND.BIN): MPU-401 output, voice k on MIDI channel k + 2, rhythm on channel 10

RolandMusic::RolandMusic(MidiDriver *midi, bool gm) : MusicDriver(kRolandLayout), _midi(midi), _gm(gm) {
}

RolandMusic::~RolandMusic() {
	stop();
	if (_midi) {
		_midi->setTimerCallback(nullptr, nullptr);
		_midi->close();
		delete _midi;
	}
}

bool RolandMusic::init(const Resources &res) {
	if (!load(res))
		return false;
	if (!_midi) {
		_logging = true;
		return true;
	}
	if (_midi->open() != 0)
		return false;
	if (_gm)
		_midi->sendGMReset();
	else
		_midi->sendMT32Reset();
	syncVolume();
	_midi->setTimerCallback(this, &RolandMusic::timerProc);
	return true;
}

void RolandMusic::timerProc(void *param) {
	RolandMusic *m = (RolandMusic *)param;
	m->_clock += (uint64)m->_midi->getBaseTempo() * kPitHz;
	const uint64 period = (uint64)kPitPerTick * 1000000;
	while (m->_clock >= period) {
		m->_clock -= period;
		m->tick();
	}
}

void RolandMusic::stop() {
	if (_midi)
		for (int c = 0; c < 16; c++)
			_midi->send(0xB0 | c, 123, 0);
}

void RolandMusic::syncVolume() {
	bool mute = ConfMan.hasKey("mute") && ConfMan.getBool("mute");
	_volume = mute ? 0 : CLIP(ConfMan.getInt("music_volume"), 0, 256);
}

void RolandMusic::send(uint8 status, uint8 a, int b) {
	if (_logging)
		logLine(b < 0 ? Common::String::format("%02X %02X", status, a)
					  : Common::String::format("%02X %02X %02X", status, a, b));
	if (!_midi)
		return;
	uint8 cmd = status & 0xF0, ch = status & 0x0F;
	if (_gm) {
		// MT-32 presets -> General MIDI; the CM-32L effect keys (> 87) have no GM sound
		if (cmd == 0xC0 && ch != 9)
			a = MidiDriver::_mt32ToGm[a & 0x7F];
		else if ((cmd == 0x80 || cmd == 0x90) && ch == 9 && a > 87)
			return;
	}
	if (cmd == 0xB0 && a == 7)
		b = b * _volume / 256;
	if (b < 0)
		_midi->send(status | (a << 8));
	else
		_midi->send(status | (a << 8) | (b << 16));
}

uint8 RolandMusic::channel(const Voice &v) const {
	// 04C1: rhythm instruments (+0D != 0) on channel 10
	return u8(v.inst + 0x0D) ? 9 : v.ch + 1;
}

void RolandMusic::noteMsg(const Voice &v, uint8 status) {
	uint8 key = u8(v.inst + 0x0A);
	if (!key)
		key = (v.note + v.transpose) & 0x7F;
	send(status | channel(v), key & 0x7F, u8(v.inst + 0x0F) & 0x7F);
}

void RolandMusic::songStarted() {
	// RPN 0 (pitch-bend range) = 24 on channels 2..5
	for (int c = 1; c < 5; c++) {
		send(0xB0 | c, 0x64, 0);
		send(0xB0 | c, 0x65, 0);
		send(0xB0 | c, 0x06, 24);
	}
}

void RolandMusic::noteStart(Voice &v, uint8 note) {
	noteMsg(v, 0x80);
	v.note = note;
	uint8 ch = channel(v);
	if (v.state == 2)
		v.level = v.fxVolume;
	send(0xB0 | ch, 7, v.level & 0x7F);
	send(0xB0 | ch, 121, 0);	// 0437
	noteMsg(v, 0x90);
}

void RolandMusic::keyOff(Voice &v) {
	noteMsg(v, 0x80);
}

void RolandMusic::slideStep(Voice &v) {
	// 0204: a new note-on without a note-off
	noteMsg(v, 0x90);
}

void RolandMusic::setInstrument(Voice &v, uint8 k) {
	if (k != v.instNo) {
		v.instNo = k;
		v.inst = kRolandLayout.instruments + 16 * k;
		send(0xC0 | channel(v), u8(v.inst + 0x0B) & 0x7F);
	}
	v.level = u8(v.inst + 0x0E);
	v.transpose = u8(v.inst + 0x0C);
}

void RolandMusic::setLevel(Voice &v, uint8 level) {
	v.level = level;
}

void RolandMusic::allOff() {
	// 021D: MPU "clear play map"
	for (int c = 0; c < 16; c++)
		send(0xB0 | c, 123, 0);
}

// ---------------------------------------------------------------------------------------------

MusicDriver *MusicDriver::create(const Resources &res) {
	MidiDriver::DeviceHandle dev = MidiDriver::detectDevice(MDT_ADLIB | MDT_MIDI | MDT_PREFER_MT32);
	MusicType type = MidiDriver::getMusicType(dev);
	if (type == MT_ADLIB) {
		AdLibMusic *m = new AdLibMusic(true);
		if (m->init(res))
			return m;
		delete m;
		return nullptr;
	}
	if (type == MT_MT32 || type == MT_GM) {
		bool gm = type == MT_GM && !ConfMan.getBool("native_mt32");
		RolandMusic *m = new RolandMusic(MidiDriver::createMidi(dev), gm);
		if (m->init(res))
			return m;
		delete m;
		return nullptr;
	}
	return nullptr;
}

MusicDriver *MusicDriver::createLogger(const Resources &res, Device device) {
	if (device == kAdLib) {
		AdLibMusic *m = new AdLibMusic(false);
		if (m->init(res))
			return m;
		delete m;
	} else {
		RolandMusic *m = new RolandMusic(nullptr, device == kGM);
		if (m->init(res))
			return m;
		delete m;
	}
	return nullptr;
}

} // End of namespace Hook
