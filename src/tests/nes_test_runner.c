#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *test_names[] =
{
	"nes_emulator_test",
	"nes_cpu_test",
	"nes_ppu_test",
	"nes_mapper_test",
	"nes_apu_test",
	"nes_audio_test",
	"execution_graph_test",
};

#define TEST_COUNT (sizeof(test_names) / sizeof(test_names[0]))

static int run_test(const char *runner_path, const char *test_name)
{
	char test_path[MAX_PATH];
	char command_line[MAX_PATH + 3];
	size_t runner_length = strlen(runner_path);
	size_t test_name_length = strlen(test_name);
	size_t directory_length = runner_length;

	while (directory_length > 0 &&
		runner_path[directory_length - 1] != '\\' &&
		runner_path[directory_length - 1] != '/')
	{
		--directory_length;
	}

	if (directory_length + test_name_length + sizeof(".exe") > sizeof(test_path))
	{
		fprintf(stderr, "[ FAIL ] %s: executable path is too long\n", test_name);
		return 1;
	}

	memcpy(test_path, runner_path, directory_length);
	memcpy(test_path + directory_length, test_name, test_name_length);
	memcpy(test_path + directory_length + test_name_length,
		".exe", sizeof(".exe"));
	snprintf(command_line, sizeof(command_line), "\"%s\"", test_path);

	STARTUPINFOA startup = { .cb = sizeof(startup) };
	PROCESS_INFORMATION process = {};
	printf("[ RUN  ] %s\n", test_name);
	fflush(stdout);

	if (!CreateProcessA(test_path, command_line, 0, 0, FALSE, 0, 0, 0,
		&startup, &process))
	{
		fprintf(stderr, "[ FAIL ] %s: could not start executable (Win32 error %lu)\n",
			test_name, GetLastError());
		return 1;
	}

	WaitForSingleObject(process.hProcess, INFINITE);
	DWORD exit_code = 1;
	if (!GetExitCodeProcess(process.hProcess, &exit_code))
	{
		fprintf(stderr, "[ FAIL ] %s: could not read exit code (Win32 error %lu)\n",
			test_name, GetLastError());
		exit_code = 1;
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);

	if (exit_code == 0)
	{
		printf("[ PASS ] %s\n\n", test_name);
	}
	else
	{
		fprintf(stderr, "[ FAIL ] %s: exited with code %lu\n\n",
			test_name, exit_code);
	}
	return exit_code != 0;
}

int main(void)
{
	char runner_path[MAX_PATH];
	DWORD path_length = GetModuleFileNameA(0, runner_path, sizeof(runner_path));
	if (path_length == 0 || path_length >= sizeof(runner_path))
	{
		fprintf(stderr, "Could not locate the test runner executable\n");
		return 1;
	}

	unsigned int failures = 0;
	for (size_t test_index = 0; test_index < TEST_COUNT; ++test_index)
	{
		failures += run_test(runner_path, test_names[test_index]);
	}

	if (failures)
	{
		fprintf(stderr, "Test suite failed: %u of %zu test executables failed\n",
			failures, TEST_COUNT);
		return 1;
	}

	printf("Test suite passed: all %zu test executables passed\n",
		TEST_COUNT);
	return 0;
}
