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

#include "cryomni3d/egypt/support/image_loader.h"

#include "common/file.h"
#include "common/memstream.h"
#include "common/path.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#include "image/tga.h"

#include "cryomni3d/egypt/support/cpx5.h"

namespace CryOmni3D {
namespace Egypt {

static bool hasCpx5Magic(Common::File &file) {
	byte magic[4] = {0, 0, 0, 0};
	if (file.size() >= 4)
		file.read(magic, sizeof(magic));
	file.seek(0);
	return memcmp(magic, "CPx5", sizeof(magic)) == 0;
}

bool loadFileMaybeCpx5(const Common::Path &path, Common::Array<byte> &data) {
	Common::File file;
	if (!file.open(path))
		return false;

	if (hasCpx5Magic(file))
		return Cpx5Decoder::decompress(file, data);

	int32 size = file.size();
	if (size <= 0)
		return false;
	data.resize((uint32)size);
	return file.read(data.data(), (uint32)size) == (uint32)size;
}

bool loadTgaImage(const Common::Path &path, Graphics::ManagedSurface &surface, bool stretchToScreen) {
	Common::File file;
	if (!file.open(path))
		return false;

	Image::TGADecoder decoder;
	bool decoded = false;
	Common::Array<byte> decompressed;
	if (hasCpx5Magic(file)) {
		if (!Cpx5Decoder::decompress(file, decompressed)) {
			warning("Egypt: failed to decompress wrapped TGA %s",
			        path.toString(Common::Path::kNativeSeparator).c_str());
			return false;
		}
		Common::MemoryReadStream stream(decompressed.data(), decompressed.size(), DisposeAfterUse::NO);
		decoded = decoder.loadStream(stream);
	} else {
		decoded = decoder.loadStream(file);
	}

	if (!decoded) {
		warning("Egypt: failed to decode TGA asset %s",
		        path.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	Graphics::Surface *converted = nullptr;
	if (decoder.hasPalette())
		converted = decoder.getSurface()->convertTo(g_system->getScreenFormat(),
		            decoder.getPalette().data(), decoder.getPalette().size());
	else
		converted = decoder.getSurface()->convertTo(g_system->getScreenFormat());

	if (!converted)
		return false;

	if (stretchToScreen) {
		surface.create(640, 480, g_system->getScreenFormat());
		surface.clear(surface.format.RGBToColor(0, 0, 0));
		surface.blitFrom(*converted, Common::Rect(0, 0, converted->w, converted->h),
		                 Common::Rect(0, 0, 640, 480));
	} else {
		surface.create(converted->w, converted->h, g_system->getScreenFormat());
		surface.blitFrom(*converted);
	}
	delete converted;
	return true;
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
