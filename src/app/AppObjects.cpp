#include "AppObjects.h"

// 成员初始化顺序 = 声明顺序；初始化列表里的参数逐字照搬拆分前装配函数里的
// 构造表达式（&database / &taskStore 解析为形参，&taskTree 等解析为成员）。

AppObjectsEarly::AppObjectsEarly(Database &database, TaskModel &taskStore)
    : taskUndo(&taskStore),
      taskTree(&database, &taskStore),
      taskStats(&database, &taskStore),
      taskExtra(&database, &taskStore),
      cardDetail(&taskStore, &taskExtra, &taskUndo),
      taskAudit(&database, &taskStore),
      taskExec(&database, &taskStore),
      agentProfile(&database),
      taskRuns(&database),
      taskRunner(&database, &taskRuns, &agentProfile, &taskExec),
      scheduler(&database, &taskRuns, &taskRunner, &agentProfile, &taskExec, &taskStore),
      workflowPresets(&database),
      workflowRunner(&database, &taskExec, &taskRuns, &taskRunner, &agentProfile,
                     &scopeMatcher, &acceptanceRunner),
      triageRunner(&database, &agentProfile, &taskExec, &taskStore),
      ledgerImport(&database, &taskStore, &taskExec),
      roadmap(&database, &taskStore),
      boardSync(&database, &taskStore),
      dataPort(&database, &taskStore)
{
    // 这两处 setter 原本穿插在成员构造之间；CardDetail / TaskRunner 构造
    // 期间不读它们，移到构造体末尾不改变任何可观察行为（AppObjects.h 有论证）。
    taskUndo.setTaskExtra(&taskExtra);
    workflowRunner.setPresets(&workflowPresets);
}

AppObjectsUi::AppObjectsUi(bool hosted)
    : themeMarket(&themePacks),
      tray(hosted)
{
}

AppObjectsPlugins::AppObjectsPlugins(Database &database, TaskModel &taskStore,
                                     TaskExtra &taskExtra, TrayController &tray)
    : reminderScheduler(&taskExtra, &tray),
      pluginLoader(&database),
      pluginRuntime(&pluginLoader, &pluginEventBus, &database, &taskStore, &tray),
      pluginHost(&pluginRuntime),
      pluginLibrary(&pluginLoader, &pluginMarket, &pluginRuntime, &database)
{
}
