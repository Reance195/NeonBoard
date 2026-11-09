#pragma once

// --selftest 只依赖这些「早已构造完成」的对象（MOD-7）。
class Database;
class TaskModel;
class TaskUndo;
class TaskTree;
class TaskExtra;
class TaskExec;
class TaskRunStore;
class WorkflowPresets;
class TriageRunner;
class RoadmapSync;
class BoardColumns;

struct SelfTestDeps {
    Database *database;
    TaskModel *taskStore;
    TaskUndo *taskUndo;
    TaskTree *taskTree;
    TaskExtra *taskExtra;
    TaskExec *taskExec;
    TaskRunStore *taskRuns;
    WorkflowPresets *workflowPresets;
    TriageRunner *triageRunner;
    RoadmapSync *roadmap;
    BoardColumns *boardColumns;
};

int runSelfTest(const SelfTestDeps &deps);
