#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct         //定义一个cpu occupy的结构体
{
	char name[20];      //定义一个char类型的数组名name有20个元素
	unsigned int user; //定义一个无符号的int类型的user
	unsigned int nice; //定义一个无符号的int类型的nice
	unsigned int system;//定义一个无符号的int类型的system
	unsigned int idle; //定义一个无符号的int类型的idle
}CPU_OCCUPY;


double cal_cpuoccupy(CPU_OCCUPY *o, CPU_OCCUPY *n)
{
	unsigned int od, nd;
	unsigned int id, sd;
	double cpu_use = 0;
	double st,tol;

	od = (unsigned int)(o->user + o->nice + o->system + o->idle);//第一次(用户+优先级+系统+空闲)的时间再赋给od
	nd = (unsigned int)(n->user + n->nice + n->system + n->idle);//第二次(用户+优先级+系统+空闲)的时间再赋给od

	id = (unsigned int)(n->user - o->user);    //用户第一次和第二次的时间之差再赋给id
	sd = (unsigned int)(n->system - o->system);//系统第一次和第二次的时间之差再赋给sd
	st=sd+id;
	tol=nd-od;
    printf("%.2f,%.2f\n",st,tol);
	if (tol != 0)
		cpu_use = (st * 100) / tol; //((用户+系统)乖100)除(第一次和第二次的时间差)再赋给g_cpu_used
	else 
		cpu_use = 0;
	//printf("cpu: %.2f\%\n", cpu_use);
	return cpu_use;
}

void get_cpuoccupy(CPU_OCCUPY *cpust) //对无类型get函数含有一个形参结构体类弄的指针O
{
	FILE *fd;
	int n;
	char buff[256];
	CPU_OCCUPY *cpu_occupy;
	cpu_occupy = cpust;

	fd = fopen("/proc/stat", "r");
	fgets(buff, sizeof(buff), fd);

	sscanf(buff, "%s %u %u %u %u", cpu_occupy->name, &cpu_occupy->user, &cpu_occupy->nice, &cpu_occupy->system, &cpu_occupy->idle);
	//printf("name = %s, user = %u, nice = %u, system = %u , idle = %u \n", cpu_occupy->name, cpu_occupy->user, cpu_occupy->nice, cpu_occupy->system, cpu_occupy->idle);

	fclose(fd);
}

int cpuuse()
{
	CPU_OCCUPY cpu_stat1;
	CPU_OCCUPY cpu_stat2;
	double cpu;
	//第一次获取cpu使用情况
	//printf("===============================cpu use================================\n");
	get_cpuoccupy((CPU_OCCUPY *)&cpu_stat1);
	sleep(5);

	//第二次获取cpu使用情况
	get_cpuoccupy((CPU_OCCUPY *)&cpu_stat2);

	//计算cpu使用率
	cpu = cal_cpuoccupy((CPU_OCCUPY *)&cpu_stat1, (CPU_OCCUPY *)&cpu_stat2);
	printf("cpu use = %.2f\%\n", cpu);
	//printf("======================================================================\n");

	return 0;
}
