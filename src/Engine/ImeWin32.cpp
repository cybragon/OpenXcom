/*
 * Copyright 2010-2016 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "ImeWin32.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <imm.h>
#include <vector>
#include <SDL.h>
#include <SDL_syswm.h>
#include "../Interface/TextEdit.h"
#include "Logger.h"

#ifdef _MSC_VER
#pragma comment(lib, "imm32.lib")
#endif

namespace OpenXcom
{
namespace Ime
{

namespace
{
HWND _hwnd = 0;
WNDPROC _sdlProc = 0;
TextEdit *_edit = 0;
bool _imeAllowed = false;
double _scaleX = 1.0, _scaleY = 1.0;
int _offX = 0, _offY = 0;
int _caretX = 0, _caretY = 0, _caretH = 8;
Uint32 _lastThink = 0;   // last TextEdit::think of the focused edit (its state is the active one)
bool _ctxOn = false;     // IME context currently associated with the window

UString readComposition(HIMC himc, DWORD which)
{
	UString out;
	LONG bytes = ImmGetCompositionStringW(himc, which, NULL, 0);
	if (bytes <= 0) return out;
	std::vector<wchar_t> buf(bytes / sizeof(wchar_t) + 1, 0);
	ImmGetCompositionStringW(himc, which, buf.data(), bytes);
	size_t n = bytes / sizeof(wchar_t);
	for (size_t i = 0; i < n; ++i)
	{
		unsigned c = (unsigned)buf[i];
		if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && (unsigned)buf[i + 1] >= 0xDC00 && (unsigned)buf[i + 1] <= 0xDFFF)
		{
			out += (UCode)(0x10000 + ((c - 0xD800) << 10) + ((unsigned)buf[i + 1] - 0xDC00));
			++i;
		}
		else if (c < 0xD800 || c > 0xDFFF)
		{
			out += (UCode)c;
		}
	}
	return out;
}

/// Delivers a committed character like a typed key (SDL keysym.unicode is 16 bit: BMP only).
void pushChar(UCode c)
{
	if (c > 0xFFFF) return;
	SDL_Event e;
	SDL_memset(&e, 0, sizeof(e));
	e.type = SDL_KEYDOWN;
	e.key.state = SDL_PRESSED;
	e.key.keysym.sym = SDLK_UNKNOWN;
	e.key.keysym.mod = KMOD_NONE;
	e.key.keysym.unicode = (Uint16)c;
	SDL_PushEvent(&e);
}

void placeWindows(HIMC himc)
{
	POINT pt;
	pt.x = _offX + (LONG)(_caretX * _scaleX);
	pt.y = _offY + (LONG)(_caretY * _scaleY);
	LONG h = (LONG)(_caretH * _scaleY);
	COMPOSITIONFORM cf;
	cf.dwStyle = CFS_POINT;
	cf.ptCurrentPos = pt;
	SetRect(&cf.rcArea, 0, 0, 0, 0);
	ImmSetCompositionWindow(himc, &cf);
	CANDIDATEFORM cand;
	cand.dwIndex = 0;
	cand.dwStyle = CFS_EXCLUDE;
	cand.ptCurrentPos.x = pt.x;
	cand.ptCurrentPos.y = pt.y + h;
	SetRect(&cand.rcArea, pt.x, pt.y, pt.x + 1, pt.y + h);
	ImmSetCandidateWindow(himc, &cand);
}

/// The focused text edit belongs to the running (top) state: it was updated recently.
bool editRunning()
{
	return _edit != 0 && _imeAllowed && SDL_GetTicks() - _lastThink < 250;
}

void applyContext()
{
	if (!_hwnd) return;
	_ctxOn = editRunning();
	if (_ctxOn)
	{
		ImmAssociateContextEx(_hwnd, NULL, IACE_DEFAULT);
		HIMC himc = ImmGetContext(_hwnd);
		if (himc)
		{
			placeWindows(himc);
			ImmReleaseContext(_hwnd, himc);
		}
	}
	else
	{
		ImmAssociateContext(_hwnd, NULL);
	}
}

bool textActive()
{
	return _ctxOn;
}

LRESULT CALLBACK imeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		if (wParam == VK_PROCESSKEY)
		{
			if (textActive())
			{
				return 0; // the IME handles this key (already passed through TranslateMessage)
			}
			// outside text fields the IME should be detached; if a key still arrives as VK_PROCESSKEY,
			// swallow it and forward the real key so hotkeys keep working in any IME mode
			UINT vk = ImmGetVirtualKey(hwnd);
			if (vk != 0 && vk != VK_PROCESSKEY)
			{
				return CallWindowProcA(_sdlProc, hwnd, msg, (WPARAM)vk, lParam);
			}
			return 0;
		}
		break;
	case WM_CHAR:
	case WM_IME_CHAR:
		if (textActive())
		{
			return 0; // committed text is read from GCS_RESULTSTR; SDL takes characters from WM_KEYDOWN
		}
		break;
	case WM_IME_SETCONTEXT:
		lParam &= ~ISC_SHOWUICOMPOSITIONWINDOW; // composition is drawn inline by the text edit
		break;
	case WM_IME_STARTCOMPOSITION:
		{
			HIMC himc = ImmGetContext(hwnd);
			if (himc)
			{
				placeWindows(himc);
				ImmReleaseContext(hwnd, himc);
			}
		}
		return 0;
	case WM_IME_COMPOSITION:
		{
			HIMC himc = ImmGetContext(hwnd);
			if (himc)
			{
				if (lParam & GCS_RESULTSTR)
				{
					UString result = readComposition(himc, GCS_RESULTSTR);
					if (_edit)
					{
						_edit->setComposition(UString());
						for (UCode c : result)
						{
							pushChar(c);
						}
					}
				}
				if (lParam & GCS_COMPSTR)
				{
					UString comp = readComposition(himc, GCS_COMPSTR);
					if (_edit)
					{
						_edit->setComposition(comp);
					}
				}
				placeWindows(himc);
				ImmReleaseContext(hwnd, himc);
			}
		}
		return 0;
	case WM_IME_ENDCOMPOSITION:
		if (_edit)
		{
			_edit->setComposition(UString());
		}
		return 0;
	default:
		break;
	}
	return CallWindowProcA(_sdlProc, hwnd, msg, wParam, lParam);
}

}

void attach()
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (SDL_GetWMInfo(&info) <= 0 || !info.window)
	{
		return;
	}
	HWND hwnd = info.window;
	if (hwnd == _hwnd && (WNDPROC)GetWindowLongPtrA(hwnd, GWLP_WNDPROC) == imeProc)
	{
		applyContext();
		return;
	}
	detach();
	WNDPROC old = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)imeProc);
	if (!old)
	{
		Log(LOG_WARNING) << "IME: could not subclass the window";
		return;
	}
	_hwnd = hwnd;
	_sdlProc = old;
	applyContext();
}

void detach()
{
	if (_hwnd && _sdlProc)
	{
		if (IsWindow(_hwnd) && (WNDPROC)GetWindowLongPtrA(_hwnd, GWLP_WNDPROC) == imeProc)
		{
			ImmAssociateContextEx(_hwnd, NULL, IACE_DEFAULT);
			SetWindowLongPtrA(_hwnd, GWLP_WNDPROC, (LONG_PTR)_sdlProc);
		}
	}
	_hwnd = 0;
	_sdlProc = 0;
}

void setGeometry(double scaleX, double scaleY, int offsetX, int offsetY)
{
	_scaleX = scaleX;
	_scaleY = scaleY;
	_offX = offsetX;
	_offY = offsetY;
}

void pump()
{
	if (_hwnd && editRunning() != _ctxOn)
	{
		applyContext(); // focused edit became (in)active, e.g. a popup state was pushed over it
	}
	if (!_hwnd || !textActive())
	{
		return; // SDL pumps the queue itself, exactly as before
	}
	MSG msg;
	while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
		{
			PostQuitMessage((int)msg.wParam);
			break;
		}
		if (msg.message == WM_KEYDOWN)
		{
			TranslateMessage(&msg);
		}
		DispatchMessageA(&msg);
	}
}

void focus(TextEdit *edit, bool allowIme)
{
	_edit = edit;
	_imeAllowed = allowIme;
	_lastThink = SDL_GetTicks();
	applyContext();
}

void think(TextEdit *edit)
{
	if (edit == _edit)
	{
		_lastThink = SDL_GetTicks();
	}
}

void unfocus(TextEdit *edit)
{
	if (_edit != edit)
	{
		return;
	}
	if (_hwnd)
	{
		HIMC himc = ImmGetContext(_hwnd);
		if (himc)
		{
			ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
			ImmReleaseContext(_hwnd, himc);
		}
	}
	_edit = 0;
	_imeAllowed = false;
	applyContext();
}

void setCaret(TextEdit *edit, int x, int y, int h)
{
	if (edit != _edit)
	{
		return;
	}
	if (x == _caretX && y == _caretY && h == _caretH)
	{
		return;
	}
	_caretX = x;
	_caretY = y;
	_caretH = h;
	if (_hwnd && textActive())
	{
		HIMC himc = ImmGetContext(_hwnd);
		if (himc)
		{
			placeWindows(himc);
			ImmReleaseContext(_hwnd, himc);
		}
	}
}

void cancelComposition(TextEdit *edit)
{
	if (edit != _edit || !_hwnd)
	{
		return;
	}
	HIMC himc = ImmGetContext(_hwnd);
	if (himc)
	{
		ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
		ImmReleaseContext(_hwnd, himc);
	}
}

}
}

#endif
