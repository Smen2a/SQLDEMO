#include "test.h"

#include <cstring>

namespace t {

std::vector<Case> &registry() {
	static std::vector<Case> cases;
	return cases;
}

static int g_failures = 0;
static int g_case_failures = 0;

void fail(const char *file, int line, const std::string &what) {
	// Cap per-test output: a broken property usually fails at hundreds of sample points.
	if (g_case_failures < 8) {
		std::printf("    FAIL %s:%d  %s\n", file, line, what.c_str());
	} else if (g_case_failures == 8) {
		std::printf("    ... further failures in this test suppressed\n");
	}
	++g_case_failures;
	++g_failures;
}

} // namespace t

int main(int argc, char **argv) {
	const char *filter = argc > 1 ? argv[1] : nullptr;
	int run = 0, failed = 0;
	for (const t::Case &c : t::registry()) {
		if (filter && !std::strstr(c.name, filter)) {
			continue;
		}
		t::g_case_failures = 0;
		c.fn();
		++run;
		if (t::g_case_failures) {
			++failed;
			std::printf("FAIL  %s (%d)\n", c.name, t::g_case_failures);
		} else {
			std::printf("ok    %s\n", c.name);
		}
	}
	std::printf("\n%d tests, %d failed\n", run, failed);
	return failed ? 1 : 0;
}
