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

#include <algorithm>
#include "ExtraSprites.h"
#include "../Engine/Surface.h"
#include "../Engine/SurfaceSet.h"
#include "../Engine/FileMap.h"
#include "../Engine/Logger.h"
#include "../Engine/Exception.h"
#include "../Engine/Unicode.h"
#include "../Engine/HiResAssets.h"
#include <vector>
#include "Mod.h"

namespace OpenXcom
{

/**
 * Creates a blank set of extra sprite data.
 */
ExtraSprites::ExtraSprites() : _current(0), _width(320), _height(200), _singleImage(false), _subX(0), _subY(0), _loaded(false)
{
}

/**
 * Cleans up the extra sprite set.
 */
ExtraSprites::~ExtraSprites()
{
}

/**
 * Loads the extra sprite set from YAML.
 * @param node YAML node.
 * @param modIndex the internal index of the associated mod.
 */
void ExtraSprites::load(const YAML::YamlNodeReader& reader, const ModData* current)
{
	reader.tryRead("type", _type);

	if (_type.empty())
	{
		std::string typeSingle;
		reader.tryRead("typeSingle", typeSingle);
		if (!typeSingle.empty())
		{
			_type = typeSingle;
			_singleImage = true;
		}
		std::string fileSingle;
		reader.tryRead("fileSingle", fileSingle);
		if (!fileSingle.empty())
		{
			_sprites[0] = fileSingle;
		}
	}

	reader.tryRead("files", _sprites);
	reader.tryRead("width", _width);
	reader.tryRead("height", _height);
	reader.tryRead("singleImage", _singleImage);
	reader.tryRead("subX", _subX);
	reader.tryRead("subY", _subY);
	_current = current;
}

/**
 * Gets the filename that this sprite represents.
 * @return The sprite name.
 */
const std::string& ExtraSprites::getType() const
{
	return _type;
}

/**
 * Gets the list of sprites defined my this mod.
 * @return The list of sprites.
 */
std::map<int, std::string> *ExtraSprites::getSprites()
{
	return &_sprites;
}

/**
 * Gets the width of the surfaces (used for single images and new spritesets).
 * @return The width of the surfaces.
 */
int ExtraSprites::getWidth() const
{
	return _width;
}

/**
 * Gets the height of the surfaces (used for single images and new spritesets).
 * @return The height of the surfaces.
 */
int ExtraSprites::getHeight() const
{
	return _height;
}

/**
 * Returns whether this is a single surface as opposed to a set of surfaces.
 * @return True if this is a single surface.
 */
bool ExtraSprites::getSingleImage() const
{
	return _singleImage;
}

/**
 * Gets the x subdivision.
 * @return The x subdivision.
 */
int ExtraSprites::getSubX() const
{
	return _subX;
}

/**
 * Gets the y subdivision.
 * @return The y subdivision.
 */
int ExtraSprites::getSubY() const
{
	return _subY;
}

/**
 * Returns if the sprite is loaded.
 * @return True/false
 */
bool ExtraSprites::isLoaded() const
{
	return _loaded;
}

/**
 * Determines if an image file is an acceptable format for the game.
 * @param filename Image filename.
 * @return True/false
 */
bool ExtraSprites::isImageFile(const std::string &filename)
{
	static const std::string exts[] = { "PNG", "GIF", "BMP", "LBM", "IFF", "PCX", "TGA", "TIF", "TIFF" };

	for (size_t i = 0; i < std::size(exts); ++i)
	{
		if (CrossPlatform::compareExt(filename, exts[i]))
			return true;
	}
	return false;
}

/**
 * Loads the external sprite into a new or existing surface.
 * @param surface Existing surface.
 * @return New surface.
 */
Surface *ExtraSprites::loadSurface(Surface *surface)
{
	if (!_singleImage)
		return surface;
	_loaded = true;

	// hi-res assets: size and palette of the image being replaced
	int origW = 0, origH = 0;
	std::vector<SDL_Color> origPalette;
	if (surface == 0)
	{
		Log(LOG_VERBOSE) << "Creating new single image: " << _type;
	}
	else
	{
		Log(LOG_VERBOSE) << "Adding/Replacing single image: " << _type;
		origW = surface->getWidth();
		origH = surface->getHeight();
		origPalette.assign(surface->getPalette(), surface->getPalette() + 256);
		delete surface;
	}
	surface = new Surface(_width, _height);
	const std::string &fileName = _sprites.begin()->second;
	// new image: no box at load time (consumers fit it, HiResAssets::fit); a 32-bit picture is kept true-colour at its size
	const bool hiRes = origW > 0
		? HiResAssets::loadReplacement(surface, fileName, origW, origH, origPalette.data(), _type)
		: HiResAssets::loadReplacement(surface, fileName, 1 << 15, 1 << 15, nullptr, _type);
	if (!hiRes)
	{
		surface->loadImage(fileName);
		if (origW > 0)
		{
			HiResAssets::logMismatch(_type, fileName, origW, origH, surface->getWidth(), surface->getHeight(),
				HiResAssets::enabled() ? "not eligible: loaded at file size as before" : "hi-res assets off: loaded at file size as before");
		}
	}
	return surface;
}

/**
 * Loads the external sprite into a new or existing surface set.
 * @param set Existing surface set.
 * @return New surface set.
 */
SurfaceSet *ExtraSprites::loadSurfaceSet(SurfaceSet *set)
{
	if (_singleImage)
		return set;
	_loaded = true;

	bool subdivision = (_subX != 0 && _subY != 0);
	int surfaceSetX = subdivision ? _subX : _width;
	int surfaceSetY = subdivision ? _subY : _height;
	if (set == 0)
	{
		Log(LOG_VERBOSE) << "Creating new surface set: " << _type;
		set = new SurfaceSet(surfaceSetX, surfaceSetY);
	}
	else
	{
		Log(LOG_VERBOSE) << "Adding/Replacing items in surface set: " << _type;
		if (set->getTotalFrames() == 0 && (set->getWidth() != surfaceSetX || set->getHeight() != surfaceSetY))
		{
			Log(LOG_VERBOSE) << "Resize empty set to: " << surfaceSetX << " x " << surfaceSetY;
			int shared = set->getMaxSharedFrames();
			*set = SurfaceSet(surfaceSetX, surfaceSetY);
			set->setMaxSharedFrames(shared);
		}
	}

	for (const auto& pair : _sprites)
	{
		int startFrame = pair.first;
		const auto& fileName = pair.second;
		if (fileName[fileName.length() - 1] == '/')
		{
			Log(LOG_VERBOSE) << "Loading surface set from folder: " << fileName << " starting at frame: " << startFrame;
			int offset = startFrame;
			std::vector<std::string> contents;
			for (const auto& f: FileMap::getVFolderContents(fileName)) { contents.push_back(f); }
			std::sort(contents.begin(), contents.end(), Unicode::naturalCompare);
			for (const auto& name : contents)
			{
				if (!isImageFile(name))
					continue;
				try
				{
					bool existed = false;
					Surface *frame = getFrame(set, offset, &existed);
					loadFrameImage(frame, existed, fileName + name);
					offset++;
				}
				catch (Exception &e)
				{
					Log(LOG_WARNING) << e.what();
				}
			}
		}
		else
		{
			if (!subdivision)
			{
				bool existed = false;
				Surface *frame = getFrame(set, startFrame, &existed);
				loadFrameImage(frame, existed, fileName);
			}
			else if (loadSheetHiRes(set, startFrame, fileName))
			{
				// hi-res sheet: existing frames are proxies of the scaled grid
			}
			else
			{
				Surface temp = Surface(_width, _height);
				temp.loadImage(fileName);
				HiResAssets::logMismatch(_type, fileName, _width, _height, temp.getWidth(), temp.getHeight(),
					HiResAssets::enabled() ? "not eligible: cut on the rule grid as before" : "hi-res assets off: cut on the rule grid as before");
				int xDivision = _width / _subX;
				int yDivision = _height / _subY;
				int frames = xDivision * yDivision;
				Log(LOG_VERBOSE) << "Subdividing into " << frames << " frames.";
				int offset = startFrame;

				for (int y = 0; y != yDivision; ++y)
				{
					for (int x = 0; x != xDivision; ++x)
					{
						Surface* frame = getFrame(set, offset);
						// for some reason regular blit() doesn't work here how i want it, so i use this function instead.
						temp.blitNShade(frame, 0 - (x * _subX), 0 - (y * _subY), 0);
						++offset;
					}
				}
			}
		}
	}
	return set;
}

/**
 * Loads one image file into a frame. An existing frame replaced by a file of another size becomes
 * a hi-res proxy (option oxceHiResAssets), otherwise the file is loaded as before.
 */
void ExtraSprites::loadFrameImage(Surface *frame, bool existed, const std::string &fileName) const
{
	const int origW = frame->getWidth(), origH = frame->getHeight();
	std::vector<SDL_Color> origPalette(frame->getPalette(), frame->getPalette() + 256);
	// box: the replaced frame, or the set frame size for a new frame
	if (HiResAssets::loadReplacement(frame, fileName, origW, origH, existed ? origPalette.data() : nullptr, _type))
		return;
	frame->loadImage(fileName);
	if (existed)
	{
		HiResAssets::logMismatch(_type, fileName, origW, origH, frame->getWidth(), frame->getHeight(),
			HiResAssets::enabled() ? "not eligible: loaded at file size as before" : "hi-res assets off: loaded at file size as before");
	}
}

/**
 * subX/subY sheet with the option oxceHiResAssets. The whole file is compared to the rule sheet size
 * (width x height): a file larger than it (w > width or h > height) gets one scale
 * max(w/width, h/height); the cells (subX x subY, as many as the rule grid has) are cut at that scale
 * (the rest of the rule grid is discarded as before) and every frame becomes a proxy of its cell.
 * A file that fits the rule sheet (e.g. one per-frame file of a cell) is loaded as before, so are the
 * following cells. 32-bit files that fit: cut at scale 1, drawn true-colour. Returns false to load the
 * sheet as before (option off, 8bpp file that fits, 32-bit not allowed -> rejected as before).
 */
bool ExtraSprites::loadSheetHiRes(SurfaceSet *set, int startFrame, const std::string &fileName)
{
	if (!HiResAssets::enabled() || _width <= 0 || _height <= 0 || _subX <= 0 || _subY <= 0)
		return false;
	std::shared_ptr<HiResAssets::Picture> pic = HiResAssets::decode(fileName);
	if (!pic)
		return false;
	const int pw = HiResAssets::pictureWidth(*pic), ph = HiResAssets::pictureHeight(*pic);
	const bool trueColor = HiResAssets::pictureTrueColor(*pic);
	if (trueColor && !HiResAssets::trueColorAllowed(_type))
	{
		Log(LOG_WARNING) << "HiResAssets: " << _type << ": " << fileName << " is a 32-bit image; only 8-bit images are allowed for this resource (it is also used in the battlescape)";
		return false;
	}
	int fw, fh;
	double scale;
	const bool larger = HiResAssets::fitBox(_width, _height, pw, ph, fw, fh, scale);
	if (!larger && !trueColor)
		return false;
	const int xDivision = _width / _subX, yDivision = _height / _subY;
	Log(LOG_INFO) << "HiResAssets: " << _type << ": " << fileName << " is " << pw << "x" << ph << (trueColor ? " (32-bit)" : " (8-bit)")
		<< ", rule sheet " << _width << "x" << _height << ": " << xDivision << "x" << yDivision << " cells of " << _subX << "x" << _subY
		<< " cut at scale " << scale << " (cell " << _subX * scale << "x" << _subY * scale << " file px)";
	const SDL_Color *qpal = HiResAssets::quantizePalette(_type, nullptr);
	int offset = startFrame;
	for (int y = 0; y != yDivision; ++y)
	{
		for (int x = 0; x != xDivision; ++x)
		{
			bool existed = false;
			Surface *frame = getFrame(set, offset, &existed);
			const double cx = x * _subX * scale, cy = y * _subY * scale;
			const double cw = std::min(_subX * scale, pw - cx), ch = std::min(_subY * scale, ph - cy);
			const int w = (int)std::lround(cw / scale), h = (int)std::lround(ch / scale);
			// the frame keeps its palette, as when the normal loader blits the cell into it
			std::vector<SDL_Color> framePal(frame->getPalette(), frame->getPalette() + 256);
			if (cw <= 0 || ch <= 0 || w <= 0 || h <= 0)
			{
				// cell outside the file: blank, as when the normal loader cuts beyond a smaller file
				HiResAssets::forget(frame->getSurface());
				*frame = Surface(_subX, _subY);
				frame->setPalette(framePal.data(), 0, 256);
			}
			else
			{
				HiResAssets::forget(frame->getSurface());
				*frame = Surface(w, h);
				frame->setPalette(framePal.data(), 0, 256);
				HiResAssets::makeProxy(frame, *pic, cx, cy, cw, ch, w, h, qpal);
				HiResAssets::attach(frame, pic, cx, cy, cw, ch, _type);
			}
			++offset;
		}
	}
	return true;
}

Surface *ExtraSprites::getFrame(SurfaceSet *set, int index, bool *existed) const
{
	int indexWithOffset = index;
	if (indexWithOffset >= set->getMaxSharedFrames())
	{
		if ((size_t)indexWithOffset >= _current->size)
		{
			std::ostringstream err;
			err << "ExtraSprites '" << _type << "' frame '" << indexWithOffset << "' exceeds mod '"<< _current->name <<"' size limit " << _current->size;
			throw Exception(err.str());
		}
		indexWithOffset += _current->offset;
	}
	else if (indexWithOffset < 0)
	{
		std::ostringstream err;
		err << "ExtraSprites '" << _type << "' frame '" << indexWithOffset << "' in mod '" << _current->name << "' is not allowed.";
		throw Exception(err.str());
	}

	Surface *frame = set->getFrame(indexWithOffset);
	if (existed)
		*existed = frame != nullptr;
	if (frame)
	{
		Log(LOG_VERBOSE) << "Replacing frame: " << index << ", using index: " << indexWithOffset;
		frame->clear();
	}
	else
	{
		Log(LOG_VERBOSE) << "Adding frame: " << index << ", using index: " << indexWithOffset;
		frame = set->addFrame(indexWithOffset);
	}
	return frame;
}

}
