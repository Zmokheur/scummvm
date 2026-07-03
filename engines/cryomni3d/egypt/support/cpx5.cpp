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

#include "cryomni3d/egypt/support/cpx5.h"

#include "common/endian.h"
#include "common/stream.h"
#include "common/textconsole.h"

namespace CryOmni3D {
namespace Egypt {

bool Cpx5Decoder::decompress(Common::SeekableReadStream &stream, Common::Array<byte> &output) {
	if (stream.size() < 12) {
		warning("CryOmni3D: CPx5 stream too short");
		return false;
	}

	char magic[5];
	magic[0] = (char)stream.readByte();
	magic[1] = (char)stream.readByte();
	magic[2] = (char)stream.readByte();
	magic[3] = (char)stream.readByte();
	magic[4] = '\0';

	if (strcmp(magic, "CPx5") != 0) {
		warning("CryOmni3D: unsupported CPx5 magic %s", magic);
		return false;
	}

	const uint32 compressedSize = stream.readUint32BE();
	const uint32 decompressedSize = stream.readUint32BE();
	if (compressedSize != stream.size()) {
		warning("CryOmni3D: CPx5 size mismatch, header=%u actual=%u", compressedSize, (uint)stream.size());
	}

	Common::Array<byte> compressedPayload;
	compressedPayload.resize(stream.size() - 12);
	if (!compressedPayload.empty())
		stream.read(compressedPayload.data(), compressedPayload.size());

	output.resize(decompressedSize);
	uint srcPos = 0;
	uint dstPos = 0;

	while (dstPos < decompressedSize) {
		if (srcPos + 4 > compressedPayload.size()) {
			warning("CryOmni3D: CPx5 truncated while reading flags");
			return false;
		}

		const uint32 flags = READ_LE_UINT32(compressedPayload.data() + srcPos);
		srcPos += 4;

		for (int bit = 31; bit >= 0 && dstPos < decompressedSize; --bit) {
			if (((flags >> bit) & 1) == 0) {
				const uint32 remaining = decompressedSize - dstPos;
				const uint32 bytesToCopy = (remaining >= 2) ? 2 : 1;
				if (srcPos + bytesToCopy > compressedPayload.size()) {
					warning("CryOmni3D: CPx5 truncated while reading literal");
					return false;
				}

				for (uint32 i = 0; i < bytesToCopy; ++i)
					output[dstPos++] = compressedPayload[srcPos++];
				continue;
			}

			if (srcPos + 2 > compressedPayload.size()) {
				warning("CryOmni3D: CPx5 truncated while reading back-reference");
				return false;
			}

			const uint16 word = READ_BE_UINT16(compressedPayload.data() + srcPos);
			srcPos += 2;

			const uint32 distance = word >> 4;
			uint32 count = word & 0x0f;
			if (count == 0)
				count = 16;

			const uint32 bytesToCopy = MIN<uint32>(count * 2, decompressedSize - dstPos);
			if (distance == 0 || distance > dstPos || dstPos + bytesToCopy > decompressedSize) {
				warning("CryOmni3D: invalid CPx5 back-reference distance=%u count=%u dst=%u/%u",
				        distance, count, dstPos, decompressedSize);
				return false;
			}

			for (uint32 i = 0; i < bytesToCopy; ++i) {
				output[dstPos] = output[dstPos - distance];
				dstPos++;
			}
		}
	}

	return true;
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
