//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"
#include <map>
#include <set>
#include <algorithm>

using std::map;
using std::set;

vector<MachineId_t> on_machines;
vector<MachineId_t> off_machines;
unsigned total_machines = -1;

// Data structures for scheduling state
map<TaskId_t, VMId_t> task_to_vm;
map<VMId_t, MachineId_t> vm_to_machine; 
map<MachineId_t, set<VMId_t>> machine_to_vms;
map<VMId_t, set<TaskId_t>> vm_to_tasks;


// Add this new method after Init()
void PrintMachineToVMs() {
    SimOutput("Current machine_to_vms mapping:", 3);
    for (const auto& [machine_id, vm_set] : machine_to_vms) {
        string vm_list = "";
        for (const auto& vm_id : vm_set) {
            vm_list += to_string(vm_id) + " ";
        }
        SimOutput("Machine " + to_string(machine_id) + " has VMs: " + vm_list, 3);
    }
}


void Scheduler::Init() {
    // Find the parameters of the clusters
    // Get the total number of machines
    // For each machine:
    //      Get the type of the machine
    //      Get the memory of the machine
    //      Get the number of CPUs
    //      Get if there is a GPU or not
    // 
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);

    total_machines = Machine_GetTotal();
    for(unsigned i = 0; i < total_machines; i++) {
        machines.push_back(MachineId_t(i));
        off_machines.push_back(MachineId_t(i));
        // Set the default state of all machines to active for greedy
        Machine_SetState (MachineId_t(i), S0);
        SimOutput("Scheduler::Init(): Created machine id of " + to_string(i), 4);
    }    
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
}

// vector<MachineId_t> getAllOnMachines(vector<MachineId_t)

void AddTaskToMachine(MachineId_t machine_id, TaskId_t task_id) {
    VMId_t vm_id = VM_Create(RequiredVMType(task_id), RequiredCPUType(task_id));
    VM_Attach(vm_id, machine_id);
    vm_to_machine[vm_id] = machine_id;
    machine_to_vms[machine_id].insert(vm_id);

    VM_AddTask(vm_id, task_id, LOW_PRIORITY);
    task_to_vm[task_id] = vm_id;
    vm_to_tasks[vm_id].insert(task_id);
    return;
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    // Get the task parameters
    TaskInfo_t task = GetTaskInfo (task_id);


    for (MachineId_t on_machine_id : on_machines) {
        if (RequiredCPUType(task_id) == Machine_GetCPUType(on_machine_id)) {
            // We know this machine is compatible with the task
            MachineInfo_t machine_info = Machine_GetInfo(on_machine_id);
            unsigned memory_left = machine_info.memory_size - machine_info.memory_used;
            if (memory_left >= GetTaskMemory(task_id)) {
                // We have enough memory on this machine
                // We need to create a new VM and put the task on it
                AddTaskToMachine(on_machine_id, task_id);
                PrintMachineToVMs();
                return;
            }
        }
    }   

    // We know we couldn't add this task to any of the on machines
    // Turn on a off machine
    for (MachineId_t off_machine_id : off_machines) {
        MachineInfo_t off_machine_info = Machine_GetInfo(off_machine_id);
        if (RequiredCPUType(task_id) == Machine_GetCPUType(off_machine_id) && off_machine_info.memory_size >= GetTaskMemory(task_id)) {
            // We have a compatible machine
            Machine_SetState(off_machine_id, S0);
            off_machines.erase(find(off_machines.begin(), off_machines.end(), off_machine_id));
            on_machines.push_back(off_machine_id);
            AddTaskToMachine(off_machine_id, task_id);
            return;
        }
    }

    // We just couldn't add this anywhere. GG
    SimOutput("Scheduler::NewTask(): Couldn't add task " + to_string(task_id) + " anywhere", 0);
}


void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
}

void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for(auto & vm: vms) {
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy
    VMId_t vm_id = task_to_vm[task_id];
    MachineId_t machine_id = vm_to_machine[vm_id];

    vm_to_machine.erase(task_to_vm[task_id]);
    machine_to_vms[machine_id].erase(vm_id);
    VM_Shutdown(task_to_vm[task_id]);

    task_to_vm.erase(task_id);
    vm_to_tasks[vm_id].erase(task_id);

    // Sort all machines in ascending order of memory used
    sort(machines.begin(), machines.end(), [](MachineId_t a, MachineId_t b) {
        return Machine_GetInfo(a).memory_used < Machine_GetInfo(b).memory_used;
    });

    // Iterate over all machines 
    for (unsigned i = 0; i < on_machines.size(); i++) {
        // Iterate over all tasks in that Machine
        MachineInfo_t machine_info = Machine_GetInfo(on_machines[i]);
        for (auto vm_id : machine_to_vms[on_machines[i]]) {
            TaskId_t task_id = *vm_to_tasks[vm_id].begin();
            TaskInfo_t task = GetTaskInfo(task_id);
            
            // For each task we want to see if we can migrate to a more loaded machine
            for (unsigned j = i + 1; j < on_machines.size(); j++) {
                MachineInfo_t target_machine_info = Machine_GetInfo(on_machines[j]);
                unsigned memory_left = target_machine_info.memory_size - target_machine_info.memory_used;

                // If we can migrate this to a more loaded machine, do it
                if (RequiredCPUType(task_id) == Machine_GetCPUType(on_machines[j]) && memory_left >= GetTaskMemory(task_id)) {
                    VM_Migrate(vm_id, on_machines[j]);

                    // Update the data structures
                    vm_to_machine[vm_id] = on_machines[j];
                    machine_to_vms[on_machines[j]].insert(vm_id);
                    machine_to_vms[on_machines[i]].erase(vm_id);
                    
                    
                }
            }
        }

        // If this machine is now empty, turn it off
        machine_info = Machine_GetInfo(on_machines[i]);
        if (machine_info.active_vms == 0) {
            Machine_SetState(on_machines[i], S5);
            SimOutput("Scheduler::TaskComplete(): Machine " + to_string(on_machines[i]) + " is now empty and is being turned off", 1);
            off_machines.push_back(on_machines[i]);
            on_machines.erase(on_machines.begin() + i);
        }
    }

    PrintMachineToVMs();
    

    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);
}

// Public interface below

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id) + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) + " completed at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    // The simulator is alerting you that machine identified by machine_id is overcommitted
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    // The function is called on to alert you that migration is complete
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " was completed at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
}

void SchedulerCheck(Time_t time) {
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);

}

void SimulationComplete(Time_t time) {
    // This function is called before the simulation terminates Add whatever you feel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;     // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
}
