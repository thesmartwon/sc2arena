#pragma once
#include <vector>
#include <string>
#include <stdint.h>
// Process manip headers
#ifdef _WIN32
#include <windows.h>

#elif defined(__APPLE__) || defined(__linux__)
#include <sys/types.h>
#include <unistd.h>
#inlcude <process.h>
#else
#error "Unsupported platform"
#endif

// https://github.com/Blizzard/s2client-api/blob/master/src/sc2utils/sc2_manage_process.cc
#ifdef _WIN32
uint64_t start_proc(std::string cmd, std::vector<std::string> args) {
	PROCESS_INFORMATION pi = { 0 };
	STARTUPINFO si = { 0 };
	si.cb = sizeof(si);

	static const unsigned int buffer_size = (1 << 16) + 1;
	char buffer[buffer_size];
	std::memset(buffer, 0, buffer_size);
	for (int i = 0; i < args.size(); ++i) {
		strcat_s(buffer, " ");
		strcat_s(buffer, args[i].c_str());
	}

	if (!CreateProcess(cmd.c_str(),									// Module name
					   buffer,										// Command line
					   NULL,										// Process handle not inheritable
					   NULL,										// Thread handle not inheritable
					   FALSE,										// Set handle inheritance to FALSE
					   NORMAL_PRIORITY_CLASS | CREATE_NEW_CONSOLE,	// No creation flags
					   NULL,										// Use parent's environment block
					   NULL,										// Use parent's starting directory
					   &si,											// Pointer to STARTUPINFO structure
					   &pi)											// Pointer to PROCESS_INFORMATION structure
		)
	{
		std::cerr << "Failed to execute process " << cmd << std::endl;
		return uint64_t(0);
	}

	return static_cast<uint64_t>(pi.dwProcessId);
}

bool kill_proc(uint64_t process_id) {
	HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, false, (DWORD)process_id);
	if (hProcess == NULL) {
		return false;
	}

	bool result = TerminateProcess(hProcess, static_cast<UINT>(-1));
	CloseHandle(hProcess);

	return result;
}

bool register_handler(void* handler) {
	return SetConsoleCtrlHandler((PHANDLER_ROUTINE)handler, true);
}
#elif defined(__linux__) || defined(__APPLE__)
uint64_t start_proc(string cmd, vector<string> args) {
	std::vector<char*> char_list;
	// execve expects the process path to be the first argument in the list.
	char_list.push_back(const_cast<char*>(args.c_str()));
	for (auto& s : args) {
		char_list.push_back(const_cast<char*>(s.c_str()));
	}

	// List needs to be null terminated for execve.
	char_list.push_back(nullptr);

	// Start the process.
	pid_t p = fork();
	if (p == 0) {
		if (execve(char_list[0], &char_list[0], nullptr) == -1) {
			std::cerr << "Failed to execute process " << char_list[0]
				<< " error: " << strerror(errno) << std::endl;
			exit(-1);
		}
	}

	return p;
}

bool kill_proc(uint64_t process_id) {
	if (kill(process_id, SIGKILL) == -1) {
		return false;
	}
	return true;
}

bool register_handler(void* handler) {
	struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;

	if (sigaction(SIGINT, &sigIntHandler, NULL) == -1) {
		return false;
	}

	return true;
}
#endif