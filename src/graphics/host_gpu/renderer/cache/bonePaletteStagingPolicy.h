#pragma once

namespace Libs::Graphics::BonePaletteStagingPolicy {

// The palette staging rewrite is an experimental diagnostic workaround, not guest semantics.
// Require an exact opt-in so normal runs preserve the bytes supplied by guest memory.
inline bool Enabled(const char* value) {
	return value != nullptr && value[0] == '1' && value[1] == '\0';
}

} // namespace Libs::Graphics::BonePaletteStagingPolicy
