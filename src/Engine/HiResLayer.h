#pragma once
/*
 * Copyright 2010-2026 OpenXcom Developers.
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
#include <SDL.h>
#include <vector>
#include <memory>
#include <unordered_map>
#include "Unicode.h"

namespace OpenXcom
{

class OpenGL;
class HiResFont;

/**
 * Kind of a deferred draw command of the hi-res overlay layer.
 * Only Glyph is implemented; Image is reserved for hi-res sprite/background
 * replacement that will be drawn by the same layer later.
 */
enum class HiResCmdKind : Uint8
{
	Glyph = 0,
	Image = 1,
};

/**
 * One deferred draw command. Geometry is always kept in BASE (game, low-res)
 * pixels, relative to the surface that currently holds the command; it is
 * translated/clipped every time the holding surface is blitted, and finally
 * converted to output pixels when the layer is rendered.
 */
struct HiResCmd
{
	HiResCmdKind kind = HiResCmdKind::Glyph;
	Sint16 x = 0, y = 0;            ///< layout box (base px): glyph advance box / image destination
	Sint16 w = 0, h = 0;
	Sint16 clipX = 0, clipY = 0;    ///< clip rect (base px), same space as x/y
	Sint16 clipW = 0, clipH = 0;
	// --- Glyph payload
	UCode code = 0;                  ///< unicode code point
	Uint8 cellH = 0;                 ///< font cell height in base px (drives the glyph pixel size)
	Uint8 refW = 0;                  ///< advance of U+AC00 in the bitmap font (base px), 0 = n/a; caps glyph width
	Uint8 fillIdx = 0;               ///< palette index of the glyph body   (font pixel value 1)
	Uint8 lineIdx = 0;               ///< palette index of the glyph outline (font pixel value 4)
	// --- Image payload (reserved)
	Uint32 imageId = 0;
	// --- resolved when the command reaches the screen buffer
	SDL_Color fill = {0, 0, 0, 0};
	SDL_Color line = {0, 0, 0, 0};
	Uint32 stamp = 0;                ///< id of the screen blit that delivered this command (occlusion)
	const void *origin = nullptr;    ///< surface that propagated this command into its current holder
};

/**
 * "Scaled-resolution drawing layer".
 *
 * Game widgets keep rendering into their 8bpp low-res surfaces exactly as before,
 * except that they may *record* hi-res draw commands instead of drawing some
 * pixels. Commands travel with the pixels: whenever a Surface is blitted onto
 * another one, its commands are translated and clipped into the target. Commands
 * that reach the screen buffer are collected per frame and rendered at the final
 * output resolution after the scaler (OpenGL path only in this prototype).
 *
 * Occlusion: every blit onto the screen buffer gets an increasing id and stamps
 * the base pixels it covers with non-transparent pixels. A command delivered by
 * blit N is visible on base pixel p iff stamp[p] <= N, i.e. nothing opaque was
 * blitted over it later (popups, cursor...).
 *
 * Palette: colours are resolved from the palette of the surface that delivers the
 * command to the screen (exactly what SDL uses to convert the 8bpp pixels), so they
 * follow palette changes; for an 8bpp screen, the screen palette at flip time is used.
 */
class HiResLayer
{
public:
	/// Global master switch; checked inline by the hooks. False = old code path only.
	static bool recording() { return _recording; }
	/// True if hi-res text is configured (options + font loaded). Drives layout of glyphs missing from bitmap fonts.
	static bool textConfigured() { return _textConfigured; }

	/// Reads options, loads fonts. Call once after SDL/options init.
	static void init();
	/// Releases everything.
	static void shutdown();
	/// Called by Screen::resetDisplay: the (new) screen buffer and whether the output path supports the layer.
	static void setScreen(SDL_Surface *screenBuffer, bool supportedOutput);
	/// Called when the GL context was re-created (textures are gone).
	static void onContextLost();

	// --- recording API (used by widgets)
	/// Adds a command to a surface (coordinates relative to it).
	static void record(SDL_Surface *holder, const HiResCmd &cmd);
	/// Surface contents were cleared.
	static void clearSurface(SDL_Surface *holder);
	/// Surface is being destroyed.
	static void forgetSurface(SDL_Surface *holder);
	/// A surface was blitted at (x,y) onto dst. Propagates commands, stamps occlusion if dst is the screen.
	static void onBlit(SDL_Surface *src, SDL_Surface *dst, int x, int y);

	// --- glyph helpers
	/// Can the hi-res font draw this code point?
	static bool canRenderGlyph(UCode c);
	/// Layout advance (base px) for a code point missing from a bitmap font, -1 if unknown.
	static int baseAdvance(UCode c, int cellH);

	// --- frame
	/// Start of a frame (screen buffer was cleared).
	static void beginFrame();
	/// Renders the frame's commands on top of the already drawn OpenGL output.
	static void renderGL(OpenGL *gl, int outW, int outH, int top, int bottom, int left, int right, const SDL_Color *screenPalette);
	/// Commands collected for the current frame (debug/tests).
	static const std::vector<HiResCmd> &frameCommands();

private:
	static bool _recording, _textConfigured;
};

}
