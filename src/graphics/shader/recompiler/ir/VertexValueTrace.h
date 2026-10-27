#pragma once

#include "graphics/shader/recompiler/ir/VertexExplodeLog.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace Libs::Graphics::ShaderRecompiler::IR {

// Diagnostic collection only. CHECK=1 supplies the existing internal fault buffer and builtins.
// KYTY_VS_VALUE_TRACE=<hex hash>:<vertex|*>:<instance|*>[:<PC>,...] (PCs include 0x prefixes).
struct VertexValueTraceSettings {
	bool enabled = false;
	bool nonfinite = false;
	uint64_t hash = 0;
	uint32_t vertex = 0, instance = 0;
	std::array<uint32_t, 16> pcs {};
	uint32_t pc_count = 0;
};

// Multiple immutable draw slots let wildcard traces survive frame-to-frame changes in the
// GPU-generated index buffer until a failing vertex invocation is encountered.
inline constexpr uint32_t VertexTraceDrawSlots = 128;

inline const VertexValueTraceSettings& VertexValueTrace() {
	static const auto settings = [] {
		VertexValueTraceSettings result;
		const char* text = KYTY_GETENV("KYTY_VS_VALUE_TRACE");
		if (text == nullptr || !VertexExplodeCheck().enabled) return result;
		char* end = nullptr;
		result.hash = std::strtoull(text, &end, 16);
		if (end == text || *end != ':' || result.hash == 0) return result;
		text = end + 1;
		if (*text == '*') {
			// Wildcard mode captures the first invalid scalar from any vertex invocation;
			// the emitter suppresses ordinary position/read events in this mode.
			result.vertex = UINT32_MAX;
			end = const_cast<char*>(text + 1);
		} else {
			const auto vertex = std::strtoull(text, &end, 0);
			if (end == text || vertex > UINT32_MAX) return result;
			result.vertex = static_cast<uint32_t>(vertex);
		}
		if (*end != ':') return result;
		text = end + 1;
		if (*text == '*') {
			result.instance = UINT32_MAX;
			end = const_cast<char*>(text + 1);
		} else {
			const auto instance = std::strtoull(text, &end, 0);
			if (end == text || instance > UINT32_MAX) return result;
			result.instance = static_cast<uint32_t>(instance);
		}
		if (*end != '\0' && *end != ':') return result;
		if (*end == ':') {
			text = end + 1;
			if (*text == '\0') return VertexValueTraceSettings {};
			while (*text != '\0') {
				const auto pc = std::strtoull(text, &end, 0);
				if (end == text || pc > UINT32_MAX || (*end != '\0' && *end != ',') ||
				    result.pc_count == result.pcs.size()) return VertexValueTraceSettings {};
				result.pcs[result.pc_count++] = static_cast<uint32_t>(pc);
				if (*end == '\0') break;
				text = end + 1;
				if (*text == '\0') return VertexValueTraceSettings {};
			}
		}
		result.enabled = true;
		const char* nf = KYTY_GETENV("KYTY_VS_NF_TRACE");
		result.nonfinite = nf != nullptr && *nf != '\0' && *nf != '0';
		return result;
	}();
	return settings;
}

inline bool VertexTraceWanted(uint64_t hash) {
	return VertexValueTrace().enabled && VertexValueTrace().hash == hash;
}

inline bool VertexTracePcWanted(uint32_t pc) {
	const auto& settings = VertexValueTrace();
	for (uint32_t i = 0; i < settings.pc_count; ++i)
		if (settings.pcs[i] == pc) return true;
	return false;
}

inline std::string VertexTraceCacheSignature() {
	const auto& settings = VertexValueTrace();
	if (!settings.enabled) return {};
	auto text = "vtxtrace=1:" + std::to_string(settings.hash) + ":" +
	            std::to_string(settings.vertex) + ":" + std::to_string(settings.instance) +
	            ":slots=" + std::to_string(VertexTraceDrawSlots);
	for (uint32_t i = 0; i < settings.pc_count; ++i) text += ":" + std::to_string(settings.pcs[i]);
	if (settings.nonfinite) text += ":nf=2";
	return text + "\n";
}

enum class VertexTraceKind : uint32_t { Buffer = 1, Image = 2, PreClip = 3, PostClip = 4, NonFinite = 5 };
inline constexpr uint32_t VertexTraceHeaderWords = 16;
inline constexpr uint32_t VertexTraceEventWords = 24;
inline constexpr uint32_t VertexTraceEvents = 32;
inline constexpr uint32_t VertexTraceSlotWords =
	VertexTraceHeaderWords + VertexTraceEvents * VertexTraceEventWords;
inline constexpr uint32_t VertexTraceWords = VertexTraceDrawSlots * VertexTraceSlotWords;
inline constexpr uint32_t VertexTraceBaseWord = ExplodeLogBaseWord + ExplodeLogWords;
inline constexpr uint32_t VertexTraceMarker = 0x56540001u;
// Header: owner claim, token lo/hi, ordinary count, image count, dropped, sequence,
// preclip ready, postclip ready. Ordinary events 0..23, images 24..29, exports 30/31.
// Event word10 means the diagnostic predicate selected this event. For images word7
// is selected_use(1) or adjacent_selected_use(2); neither proves guest EXEC provenance.
// NF mode reserves ordinary slot23; header9 means first NF ready. Kind5 records an
// invalid compiled F32 expression, not proven guest EXEC; pc is UINT32_MAX and
// resource/sampler words carry the exact IR map's XXH3 low/high, not resources.

} // namespace Libs::Graphics::ShaderRecompiler::IR
