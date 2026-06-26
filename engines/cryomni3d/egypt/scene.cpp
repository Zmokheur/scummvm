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

#include "common/endian.h"
#include "common/file.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

void CryOmni3DEngine_Egypt::parseSceneDefinition(const Common::Path &filename, const Common::String &sceneName) {
	Common::File file;
	if (!file.open(filename)) {
		warning("Egypt: failed to open scene definition %s",
		        filename.toString(Common::Path::kNativeSeparator).c_str());
		return;
	}

	_currentScene.name = sceneName;
	_currentScene.warpName = sceneName + "_24.HNM";
	_currentScene.contextName = sceneName;
	_currentScene.zones.clear();
	_currentScene.scriptLines.clear();
	_currentScene.activeZones.clear();
	_currentScene.hasWarpInit = false;
	_currentScene.hasEndInit = false;
	_currentScene.hasEndWarp = false;
	_currentScene.isFixedView = false;
	_currentCentrages.clear();

	while (!file.eos()) {
		Common::String line = file.readLine();
		line.trim();
		if (line.empty())
			continue;

		uint zoneId = 0;
		uint rawTop = 0;
		uint rawLeft = 0;
		uint rawHeight = 0;
		uint rawWidth = 0;
		uint actionId = 0;
		char commandBuffer[512];
		commandBuffer[0] = '\0';

		if (sscanf(line.c_str(), "Zone-%u %u-%u-%u-%u %u:%511[^\r\n]",
		           &zoneId, &rawTop, &rawLeft, &rawHeight, &rawWidth, &actionId, commandBuffer) == 7) {
			EgyptZone zone;
			zone.id = zoneId;
			zone.left = rawLeft;
			zone.top = rawTop;
			zone.right = rawLeft + rawWidth;
			zone.bottom = rawTop + rawHeight;
			zone.actionId = actionId;
			zone.command = Common::String(commandBuffer);
			zone.command.trim();
			parseZoneCommand(zone);
			_currentScene.zones.push_back(zone);

			warning("Egypt: zone %03u rect=(%u,%u %ux%u) action=%u command=%s param=%s label=%s extra=%s target=%s",
			        zone.id, zone.left, zone.top, rawWidth, rawHeight,
			        zone.actionId, zone.command.c_str(), zone.param.c_str(),
			        zone.label.c_str(), zone.extraParam.c_str(), zone.targetWarp.c_str());
			continue;
		}

		if (line.equalsIgnoreCase("warpinit"))
			_currentScene.hasWarpInit = true;
		else if (line.equalsIgnoreCase("endinit"))
			_currentScene.hasEndInit = true;
		else if (line.equalsIgnoreCase("endwarp"))
			_currentScene.hasEndWarp = true;

		_currentScene.scriptLines.push_back(line);
		if (line.hasPrefixIgnoreCase("centrage ")) {
			Common::String spec = line.substr(9);
			spec.trim();
			if (spec.hasSuffix("!"))
				spec.deleteLastChar();

			const char ops[] = "=+-";
			int opPos = -1;
			for (uint i = 0; i < 3; ++i) {
				opPos = spec.find(ops[i]);
				if (opPos > 0)
					break;
			}

			if (opPos > 0) {
				EgyptCentrage centrage;
				centrage.name = spec.substr(0, opPos);
				centrage.name.trim();
				centrage.op = spec[opPos];

				Common::String values = spec.substr(opPos + 1);
				values.trim();

				double alpha = 0.0;
				double beta = 0.0;
				bool parsed = false;
				if (!centrage.name.empty() && (centrage.op == '+' || centrage.op == '-')) {
					if (sscanf(values.c_str(), "%lf", &alpha) == 1 && sscanf(values.c_str(), "%lf %lf", &alpha, &beta) != 2) {
						centrage.alpha = alpha;
						parsed = true;
					}
				} else if (!centrage.name.empty() && centrage.op == '=') {
					if (sscanf(values.c_str(), "%lf %lf", &alpha, &beta) == 2) {
						centrage.alpha = alpha;
						centrage.hasBeta = true;
						centrage.beta = beta;
						parsed = true;
					}
				}

				if (!parsed) {
					warning("Egypt: unsupported centrage form in %s: %s",
					        sceneName.c_str(), line.c_str());
					logScriptLine(line);
					continue;
				}

				_currentCentrages.push_back(centrage);
				warning("Egypt: centrage %s%c%0.3f%s",
				        centrage.name.c_str(), centrage.op, centrage.alpha,
				        centrage.hasBeta ? Common::String::format(" beta=%0.3f", centrage.beta).c_str() : "");
			}
		}
		logScriptLine(line);
	}

	// Fixed-view scenes (TGA, no HNM) are identified by a "let IndiceVisuel..." line
	// in the warpinit block. All such scenes use a 640×480 TGA with screen-space zone coords.
	for (Common::Array<Common::String>::const_iterator it = _currentScene.scriptLines.begin();
	     it != _currentScene.scriptLines.end(); ++it) {
		Common::String lower = *it;
		lower.toLowercase();
		if (lower.find("indicevisuel") != Common::String::npos) {
			_currentScene.isFixedView = true;
			warning("Egypt: scene %s is a fixed-view TGA scene (IndiceVisuel detected)",
			        _currentScene.name.c_str());
			break;
		}
	}
}

void CryOmni3DEngine_Egypt::parseZoneCommand(EgyptZone &zone) {
	Common::StringTokenizer tokenizer(zone.command);
	if (tokenizer.empty())
		return;

	zone.commandName = tokenizer.nextToken();
	while (!tokenizer.empty()) {
		Common::String token = tokenizer.nextToken();
		if (token.hasPrefixIgnoreCase("WARP:")) {
			zone.targetWarp = token.substr(5);
			zone.param = zone.targetWarp;
			continue;
		}
		if (token.hasPrefixIgnoreCase("HNM:")) {
			Common::String hnmValue = token.substr(4);
			if (zone.extraParam.empty())
				zone.extraParam = hnmValue;
			else
				zone.extraParam += "/" + hnmValue;
			continue;
		}
		if (zone.label.empty())
			zone.label = token;
		else if (zone.extraParam.empty())
			zone.extraParam = token;
		else
			zone.extraParam += " " + token;
	}
}

bool CryOmni3DEngine_Egypt::inspectWarpHeader(const Common::Path &filename, EgyptWarpHeader &header) {
	Common::File file;
	if (!file.open(filename)) {
		warning("Egypt: failed to open warp %s",
		        filename.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	char tag[5];
	tag[0] = (char)file.readByte();
	tag[1] = (char)file.readByte();
	tag[2] = (char)file.readByte();
	tag[3] = (char)file.readByte();
	tag[4] = '\0';
	header.tag = tag;

	file.skip(2);
	header.audioFlags = file.readByte();
	header.bpp = file.readByte();
	header.width = file.readUint16LE();
	header.height = file.readUint16LE();

	file.skip(52);

	header.frameSize = file.readUint32LE();
	header.firstChunkSize = file.readUint32LE();

	char chunkTag[3];
	chunkTag[0] = (char)file.readByte();
	chunkTag[1] = (char)file.readByte();
	chunkTag[2] = '\0';
	header.firstChunkTag = chunkTag;

	return true;
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
