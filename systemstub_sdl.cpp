/*
 * Heart Of Darkness engine rewrite
 * Copyright (C) 2009 Gregory Montoir
 */

#include <SDL.h>
#include <stdarg.h>
#include <math.h>
#include "scaler.h"
#include "system.h"
#include "util.h"

#define SOUNDRATE_HZ 22050

struct KeyMapping {
	int keyCode;
	int mask;
};

struct System_SDL : System {
	enum {
		kCopyRectsSize = 200,
		kKeyMappingsSize = 20,
	};

	uint8_t *_offscreenBase;
	uint8_t *_offscreen;
	SDL_Surface *_screen;
	SDL_Surface *rl_screen;
	//uint16_t _pal[256];
	SDL_Color _pal[256];
	int _screenW, _screenH;
	int _shakeDx, _shakeDy;
	KeyMapping _keyMappings[kKeyMappingsSize];
	int _keyMappingsCount;
	int _scaler;
	AudioCallback _audioCb;
	
	uint8_t _gammaLut[256];

	System_SDL();
	virtual ~System_SDL();
	virtual void init(const char *title, int w, int h, bool fullscreen, bool widescreen, bool yuv);
	virtual void destroy();
	virtual void setPalette(const uint8_t *pal, int n, int depth);
	virtual void copyRect(int x, int y, int w, int h, const uint8_t *buf, int pitch);
	virtual void fillRect(int x, int y, int w, int h, uint8_t color);
	virtual void shakeScreen(int dx, int dy);
	virtual void updateScreen(bool drawWidescreen);
	virtual void processEvents();
	virtual void sleep(int duration);
	virtual uint32_t getTimeStamp();
	
	virtual void setScaler(const char *name, int multiplier);
	virtual void setGamma(float gamma);
	virtual void copyYuv(int w, int h, const uint8_t *y, int ypitch, const uint8_t *u, int upitch, const uint8_t *v, int vpitch);
	virtual void copyRectWidescreen(int w, int h, const uint8_t *buf, const uint8_t *pal);

	virtual void startAudio(AudioCallback callback);
	virtual void stopAudio();
	virtual uint32_t getOutputSampleRate();
	virtual void lockAudio();
	virtual void unlockAudio();
	virtual AudioCallback setAudioCallback(AudioCallback callback);

	void addKeyMapping(int key, uint8_t mask);
	void setupDefaultKeyMappings();
	void updateKeys(PlayerInput *inp);
	void prepareScaledGfx(int scaler);
	void switchScaledGfx(int scaler);
};

static System_SDL system_sdl;
System *const g_system = &system_sdl;

System_SDL::System_SDL() {
}

System_SDL::~System_SDL() {
}

void System_SDL::init(const char *title, int w, int h, bool fullscreen, bool widescreen, bool yuv) {
	SDL_Init(SDL_INIT_VIDEO);
	SDL_ShowCursor(SDL_DISABLE);
	SDL_WM_SetCaption(title, NULL);
	setupDefaultKeyMappings();
	memset(&inp, 0, sizeof(inp));
	_screenW = w;
	_screenH = h;
	_shakeDx = _shakeDy = 0;
	memset(_pal, 0, sizeof(_pal));
	const int offscreenSize = (w + 2) * (h + 2); // extra bytes for scalers accessing border pixels
	_offscreenBase = (uint8_t *)malloc(offscreenSize);
	if (!_offscreenBase) {
		error("System_SDL::init() Unable to allocate offscreen buffer");
	}
	memset(_offscreenBase, 0, offscreenSize);
	_offscreen = _offscreenBase + (w + 2) + 1;
	prepareScaledGfx(1);
}

void System_SDL::destroy() {
	if (_screen) {
		// free()'ed in SDL_Quit()
		_screen = 0;
	}
	free(_offscreenBase);
	_offscreenBase = 0;
}

void System_SDL::setPalette(const uint8_t *pal, int n, int depth) {
	assert(n <= 256);
	assert(depth <= 8);
	const int shift = 8 - depth;
	for (int i = 0; i < n; ++i) {
		int r = pal[i * 3 + 0];
		int g = pal[i * 3 + 1];
		int b = pal[i * 3 + 2];
		if (shift != 0) {
			r = (r << shift) | (r >> depth);
			g = (g << shift) | (g >> depth);
			b = (b << shift) | (b >> depth);
		}
		//_pal[i] = SDL_MapRGB(_screen->format, r, g, b);
		_pal[i].r = r;
		_pal[i].g = g;
		_pal[i].b = b;
	}
	SDL_SetColors(_screen, _pal, 0, 256);
}

void System_SDL::copyRect(int x, int y, int w, int h, const uint8_t *buf, int pitch) {
	assert(x >= 0 && x + w <= _screenW && y >= 0 && y + h <= _screenH);
	for (int i = 0; i < h; ++i) {
		memcpy(_offscreen + y * _screenW + x, buf, w);
		buf += pitch;
		++y;
	}
}

void System_SDL::fillRect(int x, int y, int w, int h, uint8_t color) {
	assert(x >= 0 && x + w <= _screenW && y >= 0 && y + h <= _screenH);
	for (int i = 0; i < h; ++i) {
		memset(_offscreen + y * _screenW + x, color, w);
		++y;
	}
}

void System_SDL::setScaler(const char *name, int multiplier) {
}

void System_SDL::copyYuv(int w, int h, const uint8_t *y, int ypitch, const uint8_t *u, int upitch, const uint8_t *v, int vpitch) {
}

void System_SDL::copyRectWidescreen(int w, int h, const uint8_t *buf, const uint8_t *pal) {
		return;
}

void System_SDL::setGamma(float gamma) {
	for (int i = 0; i < 256; ++i) {
		_gammaLut[i] = (uint8_t)round(pow(i / 255., 1. / gamma) * 255);
	}
}

void System_SDL::shakeScreen(int dx, int dy) {
	_shakeDx = dx;
	_shakeDy = dy;
}

static void clearScreen(uint16_t *dst, int dstPitch, int x, int y, int w, int h) {
	uint16_t *p = dst + (y * dstPitch + x) * 1;
	for (int j = 0; j < h * 1; ++j) {
		memset(p, 0, w * sizeof(uint16_t) * 1);
		p += dstPitch;
	}
}

void System_SDL::updateScreen(bool drawWidescreen) {
	SDL_LockSurface(_screen);
	uint16_t *dst = (uint16_t *)_screen->pixels;
	const int dstPitch = _screen->pitch / 2;
	const uint8_t *src = _offscreen;
	const int srcPitch = _screenW;
	int w = _screenW;
	int h = _screenH;
	if (_shakeDy > 0) {
		clearScreen(dst, dstPitch, 0, 0, w, _shakeDy);
		h -= _shakeDy;
		dst += _shakeDy * dstPitch;
	} else if (_shakeDy < 0) {
		clearScreen(dst, dstPitch, 0, 0, w, _shakeDy);
		h += _shakeDy;
		src -= _shakeDy * srcPitch;
	}
	if (_shakeDx > 0) {
		clearScreen(dst, dstPitch, 0, 0, _shakeDx, h);
		w -= _shakeDx;
		dst += _shakeDx;
	} else if (_shakeDx < 0) {
		clearScreen(dst, dstPitch, w, 0, -_shakeDx, h);
		w += _shakeDx;
		src -= _shakeDx;
	}
	SDL_SetColors(_screen, _pal, 0, 256);
	//_scalers[_scaler].scaleProc(dst, dstPitch, _pal, src, srcPitch, w, h);
	memmove(dst, src, (w*h)*1);

	
	SDL_UnlockSurface(_screen);
	
		
	SDL_BlitSurface(_screen, NULL, rl_screen, NULL);
	SDL_Flip(rl_screen);
	//SDL_UpdateRect(_screen, 0, 0, _screenW * multiplier, _screenH * multiplier);
	_shakeDx = _shakeDy = 0;
}

void System_SDL::processEvents() {
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_KEYUP:
			if (ev.key.keysym.mod & KMOD_ALT) {
				switch (ev.key.keysym.sym) {
				case SDLK_KP_PLUS:
					/*if (_scaler < _scalersCount - 1) {
						switchScaledGfx(_scaler + 1);
					}*/
					break;
				case SDLK_KP_MINUS:
					/*if (_scaler > 0) {
						switchScaledGfx(_scaler - 1);
					}*/
					break;
				default:
					break;
				}
			}
			break;
		case SDL_QUIT:
			inp.quit = true;
			break;

		}
	}
	updateKeys(&inp);
}

void System_SDL::sleep(int duration) {
	SDL_Delay(duration);
}

uint32_t System_SDL::getTimeStamp() {
	return SDL_GetTicks();
}

static void mixAudioS16(void *param, uint8_t *buf, int len) {
	System_SDL *stub = (System_SDL *)param;
	memset(buf, 0, len);
	stub->_audioCb.proc(stub->_audioCb.userdata, (int16_t *)buf, len / 2);
}

void System_SDL::startAudio(AudioCallback callback) {
	SDL_AudioSpec desired;
	memset(&desired, 0, sizeof(desired));
	desired.freq = SOUNDRATE_HZ;
	desired.format = AUDIO_S16SYS;
	desired.channels = 2;
	desired.samples = 4096;
	desired.callback = mixAudioS16;
	desired.userdata = this;
	if (SDL_OpenAudio(&desired, 0) == 0) {
		_audioCb = callback;
		SDL_PauseAudio(0);
	} else {
		error("System_SDL::startAudio() Unable to open sound device");
	}
}

void System_SDL::stopAudio() {
	SDL_CloseAudio();
}

uint32_t System_SDL::getOutputSampleRate() {
	return SOUNDRATE_HZ;
}

void System_SDL::lockAudio() {
	SDL_LockAudio();
}

void System_SDL::unlockAudio() {
	SDL_UnlockAudio();
}

AudioCallback System_SDL::setAudioCallback(AudioCallback callback) {
	SDL_LockAudio();
	AudioCallback cb = _audioCb;
	_audioCb = callback;
	SDL_UnlockAudio();
	return cb;
}

void System_SDL::addKeyMapping(int key, uint8_t mask) {
	if (_keyMappingsCount < kKeyMappingsSize) {
		for (int i = 0; i < _keyMappingsCount; ++i) {
			if (_keyMappings[i].keyCode == key) {
				_keyMappings[i].mask = mask;
				return;
			}
		}
		if (_keyMappingsCount < kKeyMappingsSize) {
			_keyMappings[_keyMappingsCount].keyCode = key;
			_keyMappings[_keyMappingsCount].mask = mask;
			++_keyMappingsCount;
		}
	}
}

void System_SDL::setupDefaultKeyMappings() {
	_keyMappingsCount = 0;
	memset(_keyMappings, 0, sizeof(_keyMappings));

	/* original key mappings of the PC version */

	addKeyMapping(SDLK_LEFT,     SYS_INP_LEFT);
	addKeyMapping(SDLK_UP,       SYS_INP_UP);
	addKeyMapping(SDLK_RIGHT,    SYS_INP_RIGHT);
	addKeyMapping(SDLK_DOWN,     SYS_INP_DOWN);
	
	addKeyMapping(SDLK_LCTRL,   SYS_INP_JUMP);
	addKeyMapping(SDLK_LSHIFT,    SYS_INP_RUN);
	addKeyMapping(SDLK_LALT,   SYS_INP_SHOOT);

	addKeyMapping(SDLK_RETURN,   SYS_INP_ESC);
}

void System_SDL::updateKeys(PlayerInput *inp) {
	inp->prevMask = inp->mask;
	uint8_t *keyState = SDL_GetKeyState(NULL);
	for (int i = 0; i < _keyMappingsCount; ++i) {
		KeyMapping *keyMap = &_keyMappings[i];
		if (keyState[keyMap->keyCode]) {
			inp->mask |= keyMap->mask;
		} else {
			inp->mask &= ~keyMap->mask;
		}
	}
}

void System_SDL::prepareScaledGfx(int scaler) {
	rl_screen = SDL_SetVideoMode(_screenW, _screenH, 16, SDL_HWSURFACE
	#ifdef SDL_TRIPLEBUF
	| SDL_TRIPLEBUF
	#else
	| SDL_DOUBLEBUF
	#endif
	);
	_screen = SDL_CreateRGBSurface(SDL_SWSURFACE, _screenW, _screenH, 8, 0, 0, 0, 0);
	//_screen = SDL_SetVideoMode(_screenW, _screenH, 8, SDL_SWSURFACE);
	if (!_screen) {
		error("System_SDL::prepareScaledGfx() Unable to allocate _screen buffer, scaler %d", scaler);
	}
	_scaler = scaler;
}

void System_SDL::switchScaledGfx(int scaler) {
	if (_scaler != scaler) {
		SDL_FreeSurface(_screen);
		_screen = 0;
		prepareScaledGfx(scaler);
	}
}

