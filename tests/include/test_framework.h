// ============================================================================
// Minimal test framework for the non-shipping NOVA test target.
// No external dependencies; plain C++ and a tiny registry.
// ============================================================================
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace novatest {

struct TestCase {
	const char* name = nullptr;
	void (*fn)() = nullptr;
};

inline std::vector<TestCase>& Registry() {
	static std::vector<TestCase> registry;
	return registry;
}

inline int& FailureCount() {
	static int failures = 0;
	return failures;
}

inline int& CheckCount() {
	static int checks = 0;
	return checks;
}

struct Registrar {
	Registrar(const char* name, void (*fn)()) {
		Registry().push_back(TestCase{ name, fn });
	}
};

inline void ReportFailure(const char* expression, const char* file, int line) {
	++FailureCount();
	std::printf("    FAIL %s:%d  %s\n", file, line, expression);
}

inline bool Check(bool condition, const char* expression, const char* file, int line) {
	++CheckCount();
	if (!condition) ReportFailure(expression, file, line);
	return condition;
}

template <typename A, typename B>
bool CheckEqual(const A& left, const B& right, const char* expression,
                const char* file, int line) {
	++CheckCount();
	if (!(left == right)) {
		ReportFailure(expression, file, line);
		return false;
	}
	return true;
}

inline int RunAll() {
	std::printf("running %d tests\n", static_cast<int>(Registry().size()));
	int failedTests = 0;
	for (const TestCase& test : Registry()) {
		const int failuresBefore = FailureCount();
		std::printf("[ RUN  ] %s\n", test.name);
		try {
			test.fn();
		} catch (const std::exception& exception) {
			ReportFailure(exception.what(), __FILE__, 0);
		} catch (...) {
			ReportFailure("unknown exception", __FILE__, 0);
		}
		if (FailureCount() != failuresBefore) {
			std::printf("[ FAIL ] %s\n", test.name);
			++failedTests;
		} else {
			std::printf("[  OK  ] %s\n", test.name);
		}
	}

	std::printf("%d checks, %d failure(s), %d/%d tests failed\n",
	            CheckCount(), FailureCount(), failedTests,
	            static_cast<int>(Registry().size()));
	return failedTests == 0 ? 0 : 1;
}

} // namespace novatest

#define NOVA_TEST(name)                                                        \
	static void name();                                                        \
	static ::novatest::Registrar nova_registrar_##name(#name, &name);          \
	static void name()

#define CHECK(expression) ::novatest::Check((expression), #expression, __FILE__, __LINE__)
#define CHECK_EQ(left, right)                                                  \
	::novatest::CheckEqual((left), (right), #left " == " #right, __FILE__, __LINE__)
