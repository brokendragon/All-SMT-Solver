#include <string>
#include <iostream>
#include <algorithm>
#include "tclap/CmdLine.h"

using namespace TCLAP;
using namespace std;

int main(int argc, char** argv)
{
	// Wrap everything in a try block.  Do this every time, 
	// because exceptions will be thrown for problems. 
	try {  

	// Define the command line object.
	CmdLine cmd("Command description message", ' ', "0.9");

	// Define a value argument and add it to the command line.
	//ValueArg<string> nameArg("n","name","Name to print",false,"","string");
	//cmd.add( nameArg );

	ValueArg<string> fileArg("", "", "file path of smt-lib2", true, "", "string");
	cmd.add(fileArg);

	ValueArg<string> timeArg("t", "time", "time out", false, "", "string");
	cmd.add(timeArg);

	// Define a switch and add it to the command line.
	//SwitchArg reverseSwitch("r","reverse","Print name backwards", false);
	//cmd.add( reverseSwitch );

	// Parse the args.
	cmd.parse( argc, argv );

	string fname = fileArg.getValue();
	std::cout << "file name: " << fname << "\n";

	if (timeArg.isSet())
	{
		int timeOut = atoi(timeArg.getValue().c_str());
		std::cout << "time out: " << timeOut << "\n";
	}

	// Get the value parsed by each arg. 
	//string name = nameArg.getValue();
	//bool reverseName = reverseSwitch.getValue();

	// Do what you intend too...
	/*if ( reverseName )
	{
		reverse(name.begin(),name.end());
		cout << "My name (spelled backwards) is: " << name << endl;
	}
	else
		cout << "My name is: " << name << endl;
*/

	} catch (ArgException &e)  // catch any exceptions
	{ cerr << "error: " << e.error() << " for arg " << e.argId() << endl; }
}

