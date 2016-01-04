/*
partition spaces of varibles before solving, depending on their sorts.
considering all variables, including free variables

can only support bitvectors which are no longer than 63bit
*/

#include <vector>
#include <stdlib.h>
#include <time.h>
#include <fstream>
#include <direct.h>
#include <Windows.h>
#include <Psapi.h>
#include <map>
#include "E:\project\Program_Analysis\z3_program\CNS\z3-4.3.2-x64-win\include\z3++.h"

using namespace z3;
#define VERBOSE
#define BIT_WIDTH 32
#define TIME_OUT 10800
#define SUBSPACE_SIZE 10

time_t startTime, endTime;
size_t memoryCost = 0;
unsigned SOLUTION_NUM = 0;
Z3_string fname = "E:\\1.smt2";//path of benchmark file

typedef struct _VARIBLE_VALUE
{
	std::string name;
	std::string type; //bool or bitvector
	unsigned size; //in bit
	std::string value;
}VARIBLE_VALUE;
std::map < std::string, VARIBLE_VALUE> defaultSolution;

//the datastructure is for search subspace
typedef struct _VARIABLE_INFO
{
	std::string name;
	std::string type; //bool or bitvector
	unsigned size; //in bit
	long long maxPos; //determined by size and SUBSPACE_SIZE
	long long pos; //where to search from
}VARIABLE_INFO;

std::vector<VARIABLE_INFO> vi;

/*
in the end of solution, output time cost, memory cost and solution number.
*/
void outputStatistics()
{
	time(&endTime);
	std::cout << "time cost: " << (endTime - startTime) << "  peak memory cost: " << memoryCost / (1024 * 1024) << "(MB)\n";
	std::cout << "solution number: " << SOLUTION_NUM << "\n";
}

/*
when timeout, exit the program
*/
VOID WINAPI countDown()
{
	int i = 0;
	while (1) {
		if (i * 600 >= TIME_OUT)
			break;//output statistics every ten minute
		i++;
		Sleep(1000 * 600);
		outputStatistics();
	}
	exit(0);
}

/*
monitor memory usage
*/
VOID WINAPI memoryMonitor()
{
	HANDLE hProc = NULL;
	PROCESS_MEMORY_COUNTERS pmc;
	SIZE_T tmp = 0;
	while (true)
	{
		hProc = GetCurrentProcess();
		GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc));
		CloseHandle(hProc);
		tmp = pmc.PagefileUsage + pmc.WorkingSetSize + pmc.QuotaNonPagedPoolUsage + pmc.QuotaPeakPagedPoolUsage;
		memoryCost = tmp > memoryCost ? tmp : memoryCost;
		Sleep(1000 * 1);//sleep 1 sec
	}
}

/*
parse a give model, to fill in a map
We use map to sort assignments
*/
void parseModel(solver *sv, model m, std::map<std::string, VARIBLE_VALUE> *res)
{
	for (unsigned j = 0; j < m.size(); j++)
	{
		Z3_ast a, av;
		VARIBLE_VALUE vv;
		context *ctx = &(sv->ctx());
		Z3_func_decl constant = Z3_get_model_constant(*ctx, m, j);
		std::string var = m[j].name().str();//get variable's name
		vv.name = var;
		Z3_sort var_sort = Z3_get_range(*ctx, m[j]);
		std::string var_type = Z3_sort_to_string(*ctx, var_sort);
		if (var_type == "Bool")
		{
			vv.type = "bool";
			Z3_eval_func_decl(*ctx, m, constant, &av);
			std::stringstream ss;
			ss << to_expr(*ctx, av);
			vv.value = ss.str();
			vv.size = 1;
		}
		else if (var_type.find("(_ BitVec", 0) == 0) //bitvector
		{
			vv.type = "bitvector";
			vv.size = Z3_get_bv_sort_size(*ctx, var_sort);
			a = Z3_mk_app(*ctx, Z3_get_model_constant(*ctx, m, j), 0, 0);
			av = a;
			Z3_eval(*ctx, m, a, &av);//get variable's value
			vv.value = Z3_get_numeral_string(*ctx, av);
		}
		else
		{
			std::cout << "we just handle bool and bitvector so far.\n";
			exit(0);
		}
		res->erase(vv.name);
		res->insert(std::make_pair(vv.name, vv));
	}
}

/*
output model to file
*/
void outputToFile(std::map<std::string, VARIBLE_VALUE> *parsedSolution, unsigned taskID)
{
	std::stringstream ss;
	ss << taskID;
	char path[MAX_PATH] = { 0 };
	GetCurrentDirectory(MAX_PATH, (LPTSTR)path);
	std::string fname = std::string(path) + "\\solutions\\solution_" + ss.str() + std::string(".txt");
	std::ofstream fs(fname);
	if (!fs.is_open())
	{
		std::cout << "failed to create solution file\n";
		exit(0);
	}
	std::map<std::string, VARIBLE_VALUE>::iterator iter;
	for (iter = parsedSolution->begin(); iter != parsedSolution->end(); iter++)
	{
		VARIBLE_VALUE vv = iter->second;
		fs << vv.name << "###(" << vv.type << ":" << vv.size << "):" << vv.value << "\n";
	}
	fs.flush();
	fs.close();
}

/*
initialize search information
*/
void initSearchInfo(std::map<std::string, VARIBLE_VALUE> *parsedSolution)
{
	std::map<std::string, VARIBLE_VALUE>::iterator iter;
	for (iter = parsedSolution->begin(); iter != parsedSolution->end(); iter++)
	{
		VARIABLE_INFO vNode;
		memset(&vNode, 0, sizeof(vNode));
		VARIBLE_VALUE vv = iter->second;
		vNode.name = vv.name;
		vNode.size = vv.size;
		vNode.type = vv.type;
		if (((long long)1 << vNode.size) % SUBSPACE_SIZE == 0)
		{
			if (((long long)1 << vNode.size) / SUBSPACE_SIZE == 0)
				vNode.maxPos = 1;
			else
				vNode.maxPos = ((long long)1 << vNode.size) / SUBSPACE_SIZE;
		}
		else
			vNode.maxPos = ((long long)1 << vNode.size) / SUBSPACE_SIZE + 1;
		vi.push_back(vNode);
	}
}

/*
assemble new constraint by limiting varaibles in subpaces
*/
void assembleCons(solver *sv)
{
	for (int i = 0; i < vi.size(); i++)
	{
		if (vi[i].maxPos == 1) continue; //indicates subspace equals space
		context *ctx = &(sv->ctx());
		if (vi[i].type == "bool")
		{
			expr var = ctx->bool_const(vi[i].name.c_str());
			if (vi[i].pos == 0)
				sv->add(var == ctx->bool_val(true));
			else 
				sv->add(var == ctx->bool_val(false));
		}
		else if (vi[i].type == "bitvector")
		{
			expr var = ctx->bv_const(vi[i].name.c_str(), vi[i].size);
			if (vi[i].pos == 0)
				sv->add(ult(var, ctx->bv_val(SUBSPACE_SIZE, vi[i].size)));
			else if (vi[i].pos == vi[i].maxPos - 1)
				sv->add(uge(var, ctx->bv_val(SUBSPACE_SIZE * (vi[i].maxPos - 1), vi[i].size)));
			else
				sv->add(uge(var, ctx->bv_val(SUBSPACE_SIZE * vi[i].pos, vi[i].size)) &&
					ult(var, ctx->bv_val(SUBSPACE_SIZE * (vi[i].pos + 1), vi[i].size)));
		}
	}
}

/*
increment postions
return false, if task is completed
*/
bool incTask()
{
	int i = 0, j = 0;
	for (i = 0; i < vi.size(); i++)
	{
		if (vi[i].pos < vi[i].maxPos - 1)
			break;
	}
	if (i == vi.size()) return false; //indicates cannot find new tasks
	vi[i].pos++;
	for (j = 0; j < i; j++)
		vi[j].pos = 0;
	return true;;
}

void negatePreviousSolution(solver *sv, std::map<std::string, VARIBLE_VALUE> *parsedSolution)
{
	context *ctx = &(sv->ctx());
	expr negate = ctx->bool_val(true);
	std::map<std::string, VARIBLE_VALUE>::iterator iter;
	for (iter = parsedSolution->begin(); iter != parsedSolution->end(); iter++)
	{
		VARIBLE_VALUE vv = iter->second;
		if (vv.type == "bool")
		{
			bool res = ((vv.value == "true") ? true : false);
			negate = negate && (ctx->bool_const(vv.name.c_str()) == ctx->bool_val(res));
		}
		else if (vv.type == "bitvector")
			negate = negate && ctx->bv_const(vv.name.c_str(), vv.size) == ctx->bv_val(vv.value.c_str(), vv.size);
		else
		{
			std::cout << "we just handle bool and bitvector so far.\n";
			exit(0);
		}
	}
	sv->add(!negate);
}

/*
find all solutions in a subspace
*/
void solveInSubspace(solver *sv)
{ 
	while (1) //do not exit, until find all solutions in the subspace
	{
		if (sv->check() == sat)
		{
			model m = sv->get_model();
			if (m)
			{
#ifdef VERBOSE
				std::cout << "model: \n" << m << "\n";
#endif
				//parse model
				std::map<std::string, VARIBLE_VALUE> parsedSolution = defaultSolution;
				parseModel(sv, m, &parsedSolution);
				outputToFile(&parsedSolution, ++SOLUTION_NUM);
				negatePreviousSolution(sv, &parsedSolution);
#ifdef VERBOSE
				std::cout << "constraints: \n" << sv->assertions() << "\n";
#endif
			}
			else return;
		}
		else
			return;
	}
}

void process(expr e)
{
	if (e.num_args() > 0)
	{
		for (int i = 0; i < e.num_args(); i++)
			process(e.arg(i));
		return;
	}
	if (e.kind() != Z3_APP_AST) return; //not a variable
	VARIBLE_VALUE vv;
	func_decl def = e.decl();
	vv.name = def.name().str();
	if (e.is_bool()) // bool
	{
		if (vv.name == "false" || vv.name == "true") return;
		vv.type = "bool";
		vv.size = 1;
		vv.value = "false";
	}
	else if (e.is_bv()) //bitvector
	{
		sort s = def.range();
		vv.type = "bitvector";
		vv.size = s.bv_size();
		vv.value = "0";
	}
	else // we do not handle other sorts
	{
		std::cout << "we just handle bool or bitvector.\n";
		exit(0);
	}
	defaultSolution.insert(std::make_pair(def.name().str(), vv));
}

/*
assign to all variables
the assignment will be replaced by z3, if the varaible is not free
*/
void getDefaultSolution(solver *sv)
{
	expr_vector ev = sv->assertions();
	for (int i = 0; i < ev.size(); i++)
		process(ev[i]);
}

/*
solving...
*/
void run()
{
	//given the problem
	context ctx;
	solver sv(ctx);
	/*expr x = ctx.bv_const("x", BIT_WIDTH),
		y = ctx.bv_const("y", BIT_WIDTH),
		z = ctx.bv_const("z", BIT_WIDTH);
	expr b = ctx.bool_const("b"),
		k = ctx.bool_const("k");
	sv.add((x > 0 && x < 10000) && (z == 2)); *///success
	//sv.add((x == 0 && y == 0 && z == 0) || (z == 3));//success
	//sv.add(x == -1 && y > -2 && y < 0);

	Z3_ast query;
	query = Z3_parse_smtlib2_file(ctx, fname, 0, 0, 0, 0, 0, 0);
	sv.add(to_expr(ctx, query));
	getDefaultSolution(&sv);
	//solving initial program, should be satisfied
	if (sv.check() == sat)
	{
		model m = sv.get_model();
		if (m)
		{
			//parse model
			std::map<std::string, VARIBLE_VALUE> parsedSolution = defaultSolution;
			parseModel(&sv, m, &parsedSolution);
			initSearchInfo(&parsedSolution);
		}
		else
		{
			std::cout << "cannot get the model of intial problem, although it is satisfied.\n";
			exit(0);
		}

	}
	else
	{
		std::cout << "initial problem is unsatisfied.\n";
		exit(0);
	}

	while (1)
	{
		sv.push();//store current state
		assembleCons(&sv);
#ifdef VERBOSE
		std::cout << "constraints: \n" << sv.assertions() << "\n";
#endif 
		solveInSubspace(&sv);
		sv.pop();
		if (incTask() == false)
			break;
	}
}

void main()
{
	system("rmdir /s /q solutions");
	system("mkdir solutions");
	time(&startTime);
	//create a timer
	DWORD threadID;
	//create a memory monitor
	if (CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)countDown, NULL, 0, &threadID) == NULL)
	{
		std::cout << "failed to create count down thread!" << "\n";
		exit(0);
	}
	//create a memory monitor
	if (CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)memoryMonitor, NULL, 0, &threadID) == NULL)
	{
		std::cout << "failed to create memory monitor thread!" << "\n";
		exit(0);
	}
	run();
	outputStatistics();
}