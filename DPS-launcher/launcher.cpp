#include <stdlib.h>
#include <time.h>
#include <Windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include "E:\project\Program_Analysis\z3_program\CNS\programs\tclap-1.2.1\include\tclap\CmdLine.h"

using namespace TCLAP;
using namespace std;

string smtFile;
unsigned TIME_OUT = 0; // in sec
unsigned MEMORY_OUT = 200; // in MB
unsigned TASK_QUEUE_LEN = 100;
unsigned PARALLEL = 1;
time_t startTime, endTime;
PROCESS_INFORMATION pi;

void showResult(std::string msg)
{
	time(&endTime);
	cout << msg << "Please check folder solutions, time cost: " << (endTime - startTime) << "\n";
}

/*
when task time out, exit
*/
VOID WINAPI countDown()
{
	Sleep(1000 * TIME_OUT);
	TerminateProcess(pi.hProcess, 0x11);
	showResult("Time out!");
	exit(0);
}

/*
configure task
*/
void Config(bool mode)
{
	char path[MAX_PATH] = { 0 };
	GetCurrentDirectory(MAX_PATH, (LPTSTR)path);
	std::string fname;
	unsigned endID = 0;
	if (mode == true)
	{
		//read endID from file
		fname = std::string(path) + "\\tasks\\endID" + std::string(".txt");
		std::ifstream fEndID(fname);
		if (!fEndID.is_open())
		{
			std::cout << "failed to open endID file\n";
			exit(0);
		}
		fEndID >> endID;
		fEndID.close();
	}
	fname = std::string(path) + "\\configure" + std::string(".txt");
	std::ofstream fConfig(fname);
	if (!fConfig.is_open())
	{
		std::cout << "failed to create configure file\n";
		exit(0);
	}
	fConfig << smtFile << "\n";
	fConfig << ((mode == true) ? 1 : 0) << "\n";
	fConfig << MEMORY_OUT << "\n";
	fConfig << PARALLEL << "\n";
	fConfig << TASK_QUEUE_LEN << "\n";
	fConfig << endID << "\n";
	fConfig.close();
}

void parseCMD(int argc, char **argv)
{
	// Wrap everything in a try block.  Do this every time, 
	// because exceptions will be thrown for problems. 
	try {

		// Define the command line object.
		CmdLine cmd("Fast all solution smt solver: based on Z3", ' ', "0.1");
		ValueArg<string> smtArg("", "", "path of smt-lib2 file", true, "", "string");
		cmd.add(smtArg);
		ValueArg<string> timeArg("t", "time", "time out: solving will be terminated when time expires", false, "", "string");
		cmd.add(timeArg);
		ValueArg<string> memArg("m", "memory", "memory out: solving will be restarted if running out of memory. 200MB in default", false, "", "string");
		cmd.add(memArg);		
		ValueArg<string> paraArg("p", "parallel", "number of working threads. 1 in default", false, "", "string");
		cmd.add(paraArg);
		ValueArg<string> lenArg("l", "len", "length of task queue. 100 in default", false, "", "string");
		cmd.add(lenArg);

		cmd.parse(argc, argv);

		smtFile = smtArg.getValue();
		if (timeArg.isSet())
			TIME_OUT = atoi(timeArg.getValue().c_str());
		if(memArg.isSet())
			MEMORY_OUT = atoi(memArg.getValue().c_str());
		if (paraArg.isSet())
			PARALLEL = atoi(paraArg.getValue().c_str());
		if (paraArg.isSet())
			PARALLEL = atoi(paraArg.getValue().c_str());
		if (lenArg.isSet())
			TASK_QUEUE_LEN = atoi(lenArg.getValue().c_str());
	}
	catch (ArgException &e)  // catch any exceptions
	{
		cerr << "error: " << e.error() << " for arg " << e.argId() << endl;
	}
}

void main(int argc, char** argv)
{
	time(&startTime);
	parseCMD(argc, argv);
	if (TIME_OUT)
	{
		DWORD threadID;
		if (CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)countDown, NULL, 0, &threadID) == NULL)
		{
			cout << "failed to create countdown thread!" << "\n";
			exit(0);
		}
	}	
	bool mode = false;
	while (1)
	{
		Config(mode);
		DWORD dwExitCode;
		STARTUPINFO si = { sizeof(si) };
		char path[MAX_PATH] = { 0 };
		GetCurrentDirectory(MAX_PATH, (LPTSTR)path);
		std::string fname = std::string(path) + "\\DynamicAsChild.exe";
		bool res = CreateProcess(NULL,
			(LPSTR)fname.c_str(),
			NULL,
			NULL,
			false,
			0,
			NULL,
			NULL,
			&si,
			&pi);
		if (res)
		{
			CloseHandle(pi.hThread);
			WaitForSingleObject(pi.hProcess, INFINITE);
			GetExitCodeProcess(pi.hProcess, &dwExitCode);
			if (dwExitCode == 0x11) //terminate by father
				Sleep(10000);
			CloseHandle(pi.hProcess);
			if (dwExitCode == 0xff) //indicates complete
				break;
		}
		mode = true;
		time_t t;
		time(&t);
		std::cout << "time cost: " << (t - startTime) << "\n";
	}
	showResult("Complete!");
	exit(0);
}