#include "graphics/host_gpu/renderer/cache/bonePaletteStagingPolicy.h"

#include <cstdio>
#include <cstdlib>

namespace {
void Check(const char* name, bool condition) {
	if (!condition) {
		std::fprintf(stderr, "BonePaletteStagingPolicyTests: %s failed\n", name);
		std::abort();
	}
}
} // namespace

int main() {
	using Libs::Graphics::BonePaletteStagingPolicy::Enabled;
	Check("unset disables", !Enabled(nullptr));
	Check("empty disables", !Enabled(""));
	Check("zero disables", !Enabled("0"));
	Check("true is not an explicit opt-in", !Enabled("true"));
	Check("prefix is not an explicit opt-in", !Enabled("10"));
	Check("exact one enables", Enabled("1"));
	std::puts("BonePaletteStagingPolicyTests: 6 checks passed");
	return 0;
}
