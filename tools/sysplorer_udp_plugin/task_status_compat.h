#pragma once
#include <stdexcept>
#include <initializer_list>

// Exact signatures declared by mw_task_manager.h. A non-null running-task
// pointer is conservatively busy, including task startup/cleanup boundaries.
// No object layout, offsets, or unknown signatures are inferred.
template<class Resolve>
bool batteryHostTaskRunning(void* manager,Resolve resolve){
    if(!manager)throw std::runtime_error("HOST_TASK_MANAGER_UNAVAILABLE");
    for(const auto name:{"?IsRunning@MwTaskManager@@QEAA_NXZ","?IsRunning@MwTaskManager@@QEBA_NXZ"}){
        const auto entry=resolve(name);
        if(entry)return reinterpret_cast<bool(*)(void*)>(entry)(manager);
    }
    for(const auto name:{"?GetRunningThread@MwTaskManager@@QEAAPEAVMwThread@@XZ","?GetRunningThread@MwTaskManager@@QEBAPEAVMwThread@@XZ"}){
        const auto entry=resolve(name);
        if(entry)return reinterpret_cast<void*(*)(void*)>(entry)(manager)!=nullptr;
    }
    throw std::runtime_error("HOST_TASK_STATUS_API_UNSUPPORTED");
}
