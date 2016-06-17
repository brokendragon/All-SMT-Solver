/*
partition the spaces of all variables dynamically, according to previous solutions.
parallelize the algorithm by multiple threads.

All tasks are stored in memory.
*/

#include <map>
#include <memory.h>
#include <vector>
#include <stdlib.h>
#include <time.h>
#include <Windows.h>
#include <Psapi.h>
#include <fstream>
#include <direct.h>
#include "E:\project\Program_Analysis\z3_program\CNS\z3-4.3.2-x64-win\include\z3++.h"

using namespace z3;

#define TIME_OUT 60 // in seconds
#define MEMORY_OUT 1024 //in MB
//#define VERBOSE
#define BIT_WIDTH 3
#define PARALLEL 4 //create worker threads
#define PREVIOUS_TASK_POOL_SIZE 100 //the length of preTaskPool

unsigned readTaskNum = 0;

Z3_string fname = "E:\\project\\Program_Analysis\\z3_program\\CNS\\benchmark\\adjust\\2.smt2";//path of benchmark file
time_t start_t = 0, end_t = 0;
SIZE_T memoryCost = 0;
unsigned TASK_ID = 0;
unsigned SOLUTION_NUM = 0;
unsigned busyWorkers = 0;
unsigned readTaskID = 0;
std::ifstream fTask; //task file
unsigned taskFileOffset = 0; //where to read in the task file
bool mode = false; // false means completely new task, true means continue from previous task
HANDLE eNumber; //access to SOLUTION_NUM exclusively
HANDLE eTaskID; //access to TASK_ID exclusively
HANDLE eTask; //access to taskPool exclusively
HANDLE eBusy; //access to busyWorkers exclusively
HANDLE eFinish; //whether the task is finish
HANDLE ePreTask; //access to preTaskPool exclusively
HANDLE eExit; //signaled when monitor does not want to exit
HANDLE eBlock; //eBlock is set, when all workers are ready to exit
HANDLE eOffset; //access to taskFileOffset exclusively

unsigned endID = 55497;

typedef struct _VARIBLE_VALUE
{
	std::string name;
	std::string type; //bool or bitvector
	unsigned size; //in bit
	std::string value; 
}VARIBLE_VALUE;

std::map < std::string, VARIBLE_VALUE> defaultSolution;

/*
Task, as its name suggests, is the core datastructure
*/
typedef struct _TASK
{
	unsigned taskID;
	unsigned fatherID;
	unsigned depth;// the variable should be explored
	unsigned direction;//0 means '<', 1 means '>', 2 means negation
	unsigned f_range; // 0 means left_border and right_border are unused, 1 means right is unused, 2 means left is unused, 3 means both are used
	std::string left_border; // if 0<x<100, left_border is 0
	std::string right_border; // if 0<x<100, right_border is 100
	unsigned tSize; // space cost for storing in task file
}TASK;
std::vector<TASK*> taskPool;
std::vector<TASK*> preTaskPool; //tasks read from files

context ctxPool[PARALLEL];
solver *reasonResourcePool[PARALLEL];//for incremental solving

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
read solutions from files, to construct constraints
*/
void readSolution(unsigned taskID, unsigned id, std::vector<VARIBLE_VALUE> *vv)
{
	std::stringstream ss;
	ss << id;
	char path[MAX_PATH] = { 0 };
	GetCurrentDirectory(MAX_PATH, (LPTSTR)path);
	std::string fname = std::string(path) + "\\solutions\\solution_" + ss.str() + std::string(".txt");
	std::ifstream fs(fname);
	if (!fs.is_open())
	{
		std::cout << "failed to read solution from file." << fname << "  " << GetLastError() << "  taskID: " << taskID <<"\n";
		exit(0);
	}
	std::string s_solution;
	VARIBLE_VALUE v_item;
	unsigned count = 0;
	while (fs >> s_solution)
	{
		count++;
		//parse solution
		size_t p1 = s_solution.find("###(", 0);
		v_item.name = s_solution.substr(0, p1);
		size_t p2 = s_solution.find(":", p1);
		v_item.type = s_solution.substr(p1 + 4, p2 - p1 - 4);
		size_t p3 = s_solution.find("):", p2);
		v_item.size = atoi(s_solution.substr(p2 + 1, p3 - p2 - 1).c_str());
		v_item.value = s_solution.substr(p3 + 2).c_str();
		vv->push_back(v_item);
	}
	fs.close();
}

/*
fill a task object
*/
void parseLine(TASK *tsk, std::string s_task)
{
	//parse task information
	size_t p1 = s_task.find(":", 0); //skip taskID
	tsk->taskID = atoi(s_task.substr(0, p1).c_str());
	size_t p2 = s_task.find(":", p1 + 1); //find fatherID
	tsk->fatherID = atoi(s_task.substr(p1 + 1, p2 - p1 - 1).c_str());
	size_t p3 = s_task.find(":", p2 + 1); //find depth
	tsk->depth = atoi(s_task.substr(p2 + 1, p3 - p2 - 1).c_str());
	size_t p4 = s_task.find(":", p3 + 1); //find direction
	tsk->direction = atoi(s_task.substr(p3 + 1, p4 - p3 - 1).c_str());
	size_t p5 = s_task.find(":", p4 + 1); //find f_range
	tsk->f_range = atoi(s_task.substr(p4 + 1, p5 - p4 - 1).c_str());
	size_t p6 = s_task.find(":", p5 + 1); //find left_border
	tsk->left_border = s_task.substr(p5 + 1, p6 - p5 - 1);
	tsk->right_border = s_task.substr(p6 + 1);
	tsk->tSize = s_task.size() + 2;
}

/*
read many tasks from files, filling waitingSet
*/
bool fillPreTaskPool()
{
	bool res = false;
	std::string line;
	unsigned count = 0;
	unsigned taskSize = 0;
	while (fTask >> line)
	{
		count++;
		res = true;
		TASK *tsk = (TASK*)malloc(sizeof(TASK));
		memset(tsk, 0, sizeof(TASK));
		parseLine(tsk, line);
		preTaskPool.push_back(tsk);
		if (count == PREVIOUS_TASK_POOL_SIZE)
			break;
	}
	return res;
}

/*
initialize a task, that is actually the orginial constraint
for example, x, y, z are all bitvectors, the constraint is 0<x<31 && 0<y<31 && 0<z<31
*/
TASK* initTask()
{
	TASK *tk = (TASK*)malloc(sizeof(TASK));
	memset(tk, 0, sizeof(TASK));
	tk->fatherID = 0;
	tk->taskID = TASK_ID++;
	return tk;
}

/*
increment SOLUTION_NUMBER exclusively
*/
void addSolutionNumber()
{
	WaitForSingleObject(eNumber, INFINITE);
	SOLUTION_NUM++; 
	SetEvent(eNumber);
}

/*
Pickup a task from the task pool
*/
TASK* pickupTask()
{
	while (true)
	{
		if (mode == false)
		{
			WaitForSingleObject(eTask, INFINITE);
			if (!taskPool.empty())
			{
				TASK *tsk = taskPool[0];
				taskPool.erase(taskPool.begin());
				SetEvent(eTask);
				return tsk;
			}
			SetEvent(eTask);
		}
		else
		{
			WaitForSingleObject(ePreTask, INFINITE);
			if (!preTaskPool.empty())
			{
				TASK *tsk = preTaskPool[0];
				preTaskPool.erase(preTaskPool.begin());
				SetEvent(ePreTask);
				return tsk;
			}
			//read tasks from files, one block a time
			if (fillPreTaskPool() == false)
				mode = false;
			readTaskNum += preTaskPool.size();
			SetEvent(ePreTask);
			continue;
		}		
		//no tasks, wait other workers to produce tasks
		WaitForSingleObject(eBusy, INFINITE);
		if (busyWorkers == 0) //no workers are working, so no new task will be produced
		{
			SetEvent(eBusy);
			return NULL;
		}
		SetEvent(eBusy);
		//wait one task finishing its work
		WaitForSingleObject(eFinish, INFINITE);
	}
	return NULL;
}

/*
assemble constraints from original constraints and subspace constraints
*/
solver* assembleCons(solver *sv, TASK *tsk, int threadSeq)
{
	try {
		if (sv->assertions().empty())//must contain orginal constraints
		{
			std::cout << "error in assembleCons: no constraints in original constraints" << "\n";
			exit(0);
		}
		if (tsk->taskID == 1) //first task
			return sv;
		//maintain prefix
		context *ctx = &sv->ctx();
		std::vector<VARIBLE_VALUE> ass;
		readSolution(tsk->taskID, tsk->fatherID, &ass);
		for (unsigned i = 0; i < tsk->depth; i++)
		{
			if (ass[i].type == "bool")
			{
				expr var = ctx->bool_const(ass[i].name.c_str());
				sv->add(var == ctx->bool_val((ass[i].value == "true") ? true : false));
			}
			else if (ass[i].type == "bitvector")
			{
				expr var = ctx->bv_const(ass[i].name.c_str(), ass[i].size);
				sv->add(var == ctx->bv_val(ass[i].value.c_str(), ass[i].size));
			}
			else // cannot handle other types so far
			{
				std::cout << "error in assembleCons, cannot handle other types.\n";
				exit(0);
			}			
		}
		//std::cout << "type: " << ass[tsk->depth].type << "\n";
		//expand last variable
		if (ass[tsk->depth].type == "bool")
		{
			expr var = ctx->bool_const(ass[tsk->depth].name.c_str());
			sv->add(var != ctx->bool_val((ass[tsk->depth].value == "true") ? true : false));
		}
		else if (ass[tsk->depth].type == "bitvector")
		{
			expr var = ctx->bv_const(ass[tsk->depth].name.c_str(), ass[tsk->depth].size);
			if (tsk->direction == 0)//'<'
			{
				if (tsk->f_range == 1 || tsk->f_range == 3)
					sv->add(ult(var, ctx->bv_val(ass[tsk->depth].value.c_str(), ass[tsk->depth].size)) && ugt(var, ctx->bv_val(tsk->left_border.c_str(), ass[tsk->depth].size)));
				else
					sv->add(ult(var, ctx->bv_val(ass[tsk->depth].value.c_str(), ass[tsk->depth].size)));
					
				//update right border
				tsk->f_range |= 2;
				tsk->right_border = ass[tsk->depth].value;
			}
			else if (tsk->direction == 1)//'>'
			{
				if (tsk->f_range == 2 || tsk->f_range == 3)
					sv->add(ugt(var, ctx->bv_val(ass[tsk->depth].value.c_str(), ass[tsk->depth].size)) && ult(var, ctx->bv_val(tsk->right_border.c_str(), ass[tsk->depth].size)));
				else
					sv->add(ugt(var, ctx->bv_val(ass[tsk->depth].value.c_str(), ass[tsk->depth].size)));
				//update left border
				tsk->f_range |= 1;
				tsk->left_border = ass[tsk->depth].value;
			}
		}			
		else // cannot handle other types so far
		{
			std::cout << "error in assembleCons, cannot handle other types.\n";
			exit(0);
		}
		
#ifdef VERBOSE
		std::cout << sv->assertions() << "\n";
#endif
		return sv;
	}
	catch (exception & ex)
	{
		std::cout << "unexpected error: " << ex << "\n";
		exit(0);
	}
}

int getAndIncrementTaskID()
{
	int td;
	WaitForSingleObject(eTaskID, INFINITE);
	td = TASK_ID++;
	SetEvent(eTaskID);
	return td;
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
		res->erase(vv.name);
		res->insert(std::make_pair(vv.name, vv));
	}
}

/*
produce one task
*/
TASK* produceATask(solver *sv, TASK *father_tsk, unsigned father_id, unsigned depth, unsigned direction)
{
	try
	{
		TASK *produced = (TASK*)malloc(sizeof(TASK));
		memset(produced, 0, sizeof(TASK));
		produced->fatherID = father_id;
		produced->depth = depth;
		produced->direction = direction; //means '<', '>', or negation		
		if (direction != 2) //bitvector
		{
			if (father_tsk)
			{
				if ((direction == 0) && (father_tsk->f_range == 1 || father_tsk->f_range == 3))//generated task is '<', e.g., x < 5
				{
					produced->f_range |= 1;
					produced->left_border = father_tsk->left_border;
				}
				else if ((direction == 1) && (father_tsk->f_range == 2 || father_tsk->f_range == 3)) //generated task is '>', e.g., x > 5
				{
					produced->f_range |= 2;
					produced->right_border = father_tsk->right_border;
				}
			}
		}
		return produced;
	}
	catch (exception & ex)
	{
		std::cout << "unexpected error: " << ex << "\n";
		exit(0);
	}
}

/*
add task to task Pool
*/
void addTask(TASK* tsk)
{
	WaitForSingleObject(eTask, INFINITE);
	tsk->taskID = getAndIncrementTaskID(); //assign task id to task here, ensuring taskPool[i+1].taskID = taskPool[i]+1. 
	taskPool.push_back(tsk);
	SetEvent(eTask);
}

/*
spawn new tasks
*/
void spawnTasks(solver *sv, TASK* tsk, std::map<std::string, VARIBLE_VALUE> *parsedModel)
{
	try {
		std::map<std::string, VARIBLE_VALUE>::iterator iter;
		unsigned count = 0;
		for (iter = parsedModel->begin(); iter != parsedModel->end(); iter++)
		{
			if (count == tsk->depth)
				break;
			count++;
		}
		std::string var_type = iter->second.type;
		TASK *n_tsk = NULL;
		if (var_type == "bool")
		{
			n_tsk = produceATask(sv, tsk, tsk->taskID, tsk->depth, 2);
			addTask(n_tsk);
		}
		else if (var_type == "bitvector")
		{
			n_tsk = produceATask(sv, tsk, tsk->taskID, tsk->depth, 0);
			addTask(n_tsk);
			n_tsk = produceATask(sv, tsk, tsk->taskID, tsk->depth, 1);
			addTask(n_tsk);
		}
		iter++;
		for (; iter != parsedModel->end(); iter++)
		{
			count++;
			std::string var_type = iter->second.type;
			TASK *n_tsk = NULL;
			if (var_type == "bool")
			{
				n_tsk = produceATask(sv, NULL, tsk->taskID, count, 2);
				addTask(n_tsk);
			}
			else if (var_type == "bitvector")
			{
				n_tsk = produceATask(sv, NULL, tsk->taskID, count, 0);
				addTask(n_tsk);
				n_tsk = produceATask(sv, NULL, tsk->taskID, count, 1);
				addTask(n_tsk);
			}
		}
	}
	catch (exception & ex)
	{
		std::cout << "unexpected error: " << ex << "\n";
		exit(0);
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
run the selected task, and produce new tasks
*/
void runTask(TASK *tsk, int threadSeq)
{
	try{	
		context ctx;
		solver sv(ctx);
		Z3_ast query;
		query = Z3_parse_smtlib2_file(ctx, fname, 0, 0, 0, 0, 0, 0);
		sv.add(to_expr(ctx, query));
		expr x = ctx.bv_const("x", BIT_WIDTH),
			y = ctx.bv_const("y", BIT_WIDTH),
			z = ctx.bv_const("z", BIT_WIDTH);
		//sv.add((x > 0 && x < 2) || (z == 2)); //success
		//sv.add((x == 0 && y == 0 && z == 0) || (z == 3));//success
		if(tsk->taskID == 1)
			getDefaultSolution(&sv);
		assembleCons(&sv, tsk, threadSeq);
		if (sv.check())//sat?
		{
			model m = sv.get_model();
			if (m)//has a model?
			{
#ifdef VERBOSE
				std::cout << tsk->taskID << " sat, model is: \n" << m << "\n";
#endif
				std::map<std::string, VARIBLE_VALUE> parsedModel = defaultSolution;				
				parseModel(&sv, m, &parsedModel);
				outputToFile(&parsedModel, tsk->taskID);
				addSolutionNumber();
				spawnTasks(&sv, tsk, &parsedModel);
			}
		}
	}
	catch (exception & ex)
	{
		std::cout << "unexpected error: " << ex << "\n";
		exit(0);
	}
}


VOID WINAPI threadMain(LPVOID pThreadSeq)
{
	int ts = *(int*)pThreadSeq;
	free(pThreadSeq);
	TASK *taskToRun = NULL;
	while (taskToRun = pickupTask())
	{
		//std::cout << taskToRun->taskID << "  ";
		WaitForSingleObject(eBusy, INFINITE);
		busyWorkers++;
		SetEvent(eBusy);
		runTask(taskToRun, ts);
		WaitForSingleObject(eOffset, INFINITE);
		taskFileOffset += taskToRun->tSize;
		free(taskToRun);
		SetEvent(eOffset);
		//remind task is finished
		SetEvent(eFinish);
		ResetEvent(eFinish);
		WaitForSingleObject(eBusy, INFINITE);
		busyWorkers--;		
		SetEvent(eBusy);
		if (WAIT_TIMEOUT == WaitForSingleObject(eExit, 0)) //no sigal, have to exit
			break;
	}
	WaitForSingleObject(eBusy, INFINITE);
	if (busyWorkers == 0) //all workers exit
		SetEvent(eBlock);
	SetEvent(eBusy);
}

void setAllEvents()
{
	if ((eNumber = CreateEvent(NULL, false, true, NULL)) == NULL ||
		(eTaskID = CreateEvent(NULL, false, true, NULL)) == NULL ||
		(eTask = CreateEvent(NULL, false, true, NULL)) == NULL ||
		(ePreTask = CreateEvent(NULL, false, true, NULL)) == NULL ||
		(eBusy = CreateEvent(NULL, false, true, NULL)) == NULL ||
		(eFinish = CreateEvent(NULL, true, false, NULL)) == NULL ||
		(eExit = CreateEvent(NULL, true, true, NULL)) == NULL ||
		(eBlock = CreateEvent(NULL, false, false, NULL)) == NULL ||
		(eOffset = CreateEvent(NULL, false, true, NULL)) == NULL)
	{
		std::cout << "failed to create events \n";
		exit(0);
	}
}

void deleteAllEvents()
{
	CloseHandle(eNumber);
	CloseHandle(eTaskID);
	CloseHandle(eTask);
	CloseHandle(ePreTask);
	CloseHandle(eBusy);
	CloseHandle(eFinish);
	//CloseHandle(eExit);
	CloseHandle(eBlock);
	CloseHandle(eOffset);
}

/*
in the end of solution, output time cost, memory cost and solution number.
*/
void outputStatistics()
{
	std::cout << "time cost: " << ((end_t == 0) ? TIME_OUT: (end_t - start_t)) << " (s), peak memory cost: " << memoryCost / (1024 * 1024) << "(MB)\n";
	WaitForSingleObject(eNumber, INFINITE);
	std::cout << "solution number: " << SOLUTION_NUM << "\n";
	SetEvent(eNumber);
}

/*
Dump all tasks that have not been executed.
*/
void DumpTasks()
{
	WaitForSingleObject(eTask, INFINITE);
	char path[MAX_PATH] = { 0 };
	GetCurrentDirectory(MAX_PATH, (LPTSTR)path);
	std::string fname = std::string(path) + "\\tasks\\offset" + std::string(".txt");
	std::ofstream fs(fname);
	if (!fs.is_open())
	{
		std::cout << "failed to create offset file\n";
		exit(0);
	}
	fs << taskFileOffset << "\n";
	fs.close();

	fname = std::string(path) + "\\tasks\\task" + std::string(".txt");	
	fs.open(fname, std::ios::app);
	if (!fs.is_open())
	{
		std::cout << "failed to create task file\n";
		exit(0);
	}
	for (int i = 0; i < taskPool.size(); i++)
	{
		TASK *tsk = taskPool[i];		
		fs << tsk->taskID << ":" << tsk->fatherID << ":" << tsk->depth << ":" << tsk->direction << ":" << tsk->f_range;
		if (tsk->f_range == 0)
			fs << ":*:*\n";
		else if (tsk->f_range == 1)
			fs << ":" << tsk->left_border << ":*\n";
		else if (tsk->f_range == 2)
			fs << ":*:" << tsk->right_border << "\n";
		else
			fs << ":" << tsk->left_border << ":" << tsk->right_border << "\n";
	}
	fs.flush();
	fs.close();
	if(!taskPool.empty())
		std::cout << "dump tasks start from " << taskPool[0]->taskID << " to " << taskPool[taskPool.size() - 1]->taskID << "\n";
	taskPool.clear();
	std::cout << "task file offset: " << taskFileOffset << "\n";
	SetEvent(eTask);
}

/*
when task time out, exit
*/
VOID WINAPI countDown()
{
	Sleep(1000 * TIME_OUT);
	ResetEvent(eExit);
	WaitForSingleObject(eBlock, INFINITE);
	//so far, all workers have exited
	fTask.close();
	outputStatistics();
	DumpTasks();
	exit(0);
}

/*
monitor memory consumption information
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
		if (memoryCost > (SIZE_T)MEMORY_OUT * (SIZE_T)1024 * (SIZE_T)1024) //eat too much memory
		{
			outputStatistics();
			exit(0);
		}
		Sleep(1000 * 1);//sleep 1 sec
	}
}

void getTaskFileOffset()
{
	char path[MAX_PATH] = { 0 };
	GetCurrentDirectory(MAX_PATH, (LPTSTR)path);
	std::string fname = std::string(path) + "\\tasks\\offset" + std::string(".txt");
	std::ifstream fs(fname);
	if (!fs.is_open())
	{
		std::cout << "failed to open offset file\n";
		exit(0);
	}
	fs >> taskFileOffset;
	fs.close();
}

void openTaskFile()
{
	char path[MAX_PATH] = { 0 };
	GetCurrentDirectory(MAX_PATH, (LPTSTR)path);
	std::string fname = std::string(path) + "\\tasks\\task" + std::string(".txt");
	fTask.open(fname);
	if (!fTask.is_open())
	{
		std::cout << "failed to open task file\n";
		exit(0);
	}
	fTask.seekg(taskFileOffset);
}

void main()
{
	if (!mode) //completely new task
	{
		mode = false;
		system("rmdir /s /q solutions");
		system("rmdir /s /q tasks");
		system("mkdir solutions");
		system("mkdir tasks");
	}
	else
	{
		TASK_ID = endID + 1;
		getTaskFileOffset();
		openTaskFile();
	}
	//create a timer
	DWORD threadID;
	if (CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)countDown, NULL, 0, &threadID) == NULL)
	{
		std::cout << "failed to create countdown thread!" << "\n";
		exit(0);
	}
	//create a memory monitor
	if (CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)memoryMonitor, NULL, 0, &threadID) == NULL)
	{
		std::cout << "failed to create memory monitor thread!" << "\n";
		exit(0);
	}
	time(&start_t);
	if (mode == false)
	{
		TASK *originalTask = initTask();
		addTask(originalTask);
	}	
	setAllEvents();	
	HANDLE hThread[PARALLEL];
	for (int i = 0; i < PARALLEL; i++)
	{
		DWORD threadID;
		int *pdata = (int*)malloc(sizeof(int));
		*pdata = i;
		if ((hThread[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)threadMain, pdata, 0, &threadID)) == NULL)
		{
			std::cout << "failed to create worker thread!" << "\n";
			exit(0);
		}
	}
	//Wait until all threads have terminated.
	WaitForMultipleObjects(PARALLEL, hThread, TRUE, INFINITE);
	//Closeallthreadhandlesuponcompletion.
	for (int i = 0; i < PARALLEL; i++)
		CloseHandle(hThread[i]);
	deleteAllEvents();
	time(&end_t);
	WaitForSingleObject(eExit, INFINITE); //if task is completed, eExit is signaled
	outputStatistics();
	std::cout << "read task number: " << readTaskNum << "\n";
}