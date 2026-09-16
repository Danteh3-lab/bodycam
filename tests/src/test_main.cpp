#include "test_framework.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

int main() {
#ifdef _MSC_VER
	// Never stop a test run behind a modal assertion dialog: report to stderr
	// so CI and console runs see the exact assertion and location.
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
	return novatest::RunAll();
}
