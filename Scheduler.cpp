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

enum MachinePowerState {
    ON = 0,
    TRANSITION = 1, 
    OFF = 2,
};

struct MachineState { 
    set<VMId_t> vms;
    MachinePowerState state;
};

const MachineState_t SLEEP_STATE = S2; // State we initially shut down empty PM to

// Progress Bar
unsigned total_tasks = 0;
unsigned completed_tasks = 0;

// Data Structures for state tracking
unsigned total_machines = -1;
unsigned total_on_machines = -1;

map<TaskId_t, VMId_t> task_assignments;
vector<VMId_t> vms;
map<MachineId_t, MachineState> machine_states;
map<MachineId_t, vector<TaskId_t>> pending_attachments; // We want to add VMs to a machine that is transitioning to ON but isnt ON yet.
map<VMId_t, MachineId_t> ongoing_migrations;


void Add_TaskToMachine(MachineId_t machine_id, TaskId_t task_id) {
    VMId_t vm_id = VM_Create(RequiredVMType(task_id), RequiredCPUType(task_id));
    VM_Attach(vm_id, machine_id);
    machine_states[machine_id].vms.insert(vm_id);
    VM_AddTask(vm_id, task_id, LOW_PRIORITY);
    task_assignments[task_id] = vm_id;
    SimOutput("NewTask(): Added " + to_string(task_id) + " on vm: " + to_string(vm_id) + " to on machine " + to_string(machine_id), 1);
    return;
}


// Add this new method after Init()
void PrintMachineToVMs() {
    // SimOutput("Current machine_to_vms mapping:", 1);
    string res = "TOTAL: \n";
    for (const auto& [machine_id, m_state] : machine_states) {
        string vm_list = "";
        for (const auto& vm_id : m_state.vms) {
            vm_list += to_string(vm_id) + " (" + to_string(VM_GetInfo(vm_id).active_tasks.size()) + ") ";
        }
        if (Machine_GetInfo(machine_id).active_vms > 0)
            res += "Machine " + to_string(machine_id) + " ours: " + to_string(m_state.vms.size()) + " vs sim: " + to_string(Machine_GetInfo(machine_id).active_vms) + " has VMs: " + vm_list + "\n";
    }
    SimOutput(res, 1);
}

bool Machine_IsMigrationTarget(MachineId_t machine_id) {
    // Check if this machine is a target for migration
    for (const auto& [vm_id, m_id] : ongoing_migrations) {
        if (m_id == machine_id) return true;
    }
    return false;
}


void Scheduler::Init() {
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);

    total_machines = Machine_GetTotal();
    total_on_machines = total_machines;
    total_tasks = GetNumTasks();
    for(unsigned i = 0; i < total_machines; i++) {
        machines.push_back(MachineId_t(i));
        machine_states[i] = {{}, ON};
        // What is the default state of all machines for greedy, sleeping or on?
        SimOutput("Scheduler::Init(): Created machine id of " + to_string(i), 4);
    }    
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
    SimOutput("MigrationComplete(): Migration of VM " + to_string(vm_id) + " completed at time " + to_string(time), 1);
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    machine_states[vm_info.machine_id].vms.insert(vm_id);
    ongoing_migrations.erase(vm_id);

    // If there are no tasks on this VM, we can shut it down
    if (VM_GetInfo(vm_id).active_tasks.size() == 0) {
        // We can shutdown this VM
        SimOutput("MigrationComplete(): VM " + to_string(vm_id) + " is now empty and is being shut down", 1);
        VM_Shutdown(vm_id);
        // Remove the VM from the machine
        MachineId_t m_id = vm_info.machine_id;
        machine_states[m_id].vms.erase(vm_id);
    }
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    SimOutput("NewTask(): New task at time: " + to_string(now), 1);
    // Get the task parameters
    TaskInfo_t task = GetTaskInfo (task_id);

    // Try adding to an on machine
    for (auto& [machine_id, m_state] : machine_states) {
        // If this machine is on and is the right CPU type and has memory left
        if (m_state.state == ON && RequiredCPUType(task_id) == Machine_GetCPUType(machine_id)) {
            // Calculate the memory left
            MachineInfo_t machine_info = Machine_GetInfo(machine_id);
            unsigned memory_left = machine_info.memory_size - machine_info.memory_used;
            if (memory_left >= GetTaskMemory(task_id)) {
                // We have enough memory on this machine
                // We need to create a new VM and put the task on it
                Add_TaskToMachine(machine_id, task_id);
                PrintMachineToVMs();
                return;
            }
        }
    }
    // We know we couldn't add this task to any of the on machines
    // Turn on a off machine
    // WONT WORK - COMPLICATED!!! We haev to wait til the machine comes back online in order to add?
    // So we have to use the state completed method at the bottom?
    for (auto& [machine_id, m_state] : machine_states) {
        MachineInfo_t off_machine_info = Machine_GetInfo(machine_id);
        if (m_state.state == OFF &&
            RequiredCPUType(task_id) == Machine_GetCPUType(machine_id) &&
            off_machine_info.memory_size >= GetTaskMemory(task_id)) {
            // We have a compatible machine
            Machine_SetState(machine_id, S0);
            machine_states[machine_id].state = TRANSITION;
            pending_attachments[machine_id].push_back(task_id);
            
            SimOutput("NewTask(): Added PENDING " + to_string(task_id) + " to on machine " + to_string(machine_id), 1);
            PrintMachineToVMs();
            return;
        }
    }

    // If we still couldn't find anything, lets just add it to the first CPU compatible machine
    for (auto& [machine_id, m_state] : machine_states) {
       
        if (RequiredCPUType(task_id) == Machine_GetCPUType(machine_id)) {
             // Add to pending or immediately depending on ON/OFF state
            if (m_state.state == OFF || m_state.state == TRANSITION) {
                Machine_SetState(machine_id, S0);
                machine_states[machine_id].state = TRANSITION;
                pending_attachments[machine_id].push_back(task_id);
                SimOutput("NewTask(): Added overutilized PENDING " + to_string(task_id) + " to on machine " + to_string(machine_id), 1);
                PrintMachineToVMs();
                return;
            } else if (m_state.state == ON) {
                // We need to create a new VM and put the task on it
                Add_TaskToMachine(machine_id, task_id);
                SimOutput("NewTask(): Added overutilized " + to_string(task_id) + " to on machine " + to_string(machine_id), 1);
                PrintMachineToVMs();
                return;
            }
            SimOutput("the state of machine is " + to_string(m_state.state), 1);
        }
    } 

    // We just couldn't add this anywhere. TBD: We need to just add this on top of some machine and let it overutilize memory.
    // We could also have to add it on top of a machine in transition, add as pending task
    ThrowException("Scheduler::NewTask(): Couldn't add task " + to_string(task_id) + " anywhere with " + to_string(total_on_machines) + " machines");
    // SimOutput("Scheduler::NewTask(): Couldn't add task " + to_string(task_id) + " anywhere", 1);
}

void DisplayProgressBar() {
    int barWidth = 70;
    float progress = (float)completed_tasks / total_tasks;
    // std::cout << "\033[2J\033[1;1H"; // Clear the console]]"
    std::cout << "[";
    int pos = barWidth * progress;
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << int(progress * 100.0) << " %\r";
    std::cout.flush();
}

void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
    DisplayProgressBar();
}

void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for (auto id : vms) {
        VM_Shutdown(id);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy 

    VMId_t vm_id = task_assignments[task_id];
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now) + " on vm " + to_string(vm_id), 1);
 
    if (VM_GetInfo(vm_id).active_tasks.size() != 0) {
        ThrowException("Somehow there are active tasks on a VM?????");
    }

    // PROGRESS
    completed_tasks++;

    // First, delete the task
    task_assignments.erase(task_id);
    // Delete the VM - only do this because there was only one task
    MachineId_t orig_m_id = vm_info.machine_id;
    machine_states[orig_m_id].vms.erase(vm_id);
    SimOutput("Shutdown(): vm " + to_string(vm_id), 1);

    // The problem is trying to shut down a VM that is going through migration
    if (!ongoing_migrations.count(vm_id))
        VM_Shutdown(vm_id);

    // Sort all machines in ascending order of memory used
    vector<pair<MachineId_t, MachineState>> sorted_machines;
    for (const auto& [machine_id, m_state] : machine_states) {
        if (m_state.state == ON) sorted_machines.emplace_back(machine_id, m_state);
    }
    sort(sorted_machines.begin(), sorted_machines.end(), [](const pair<MachineId_t, MachineState>& a, const pair<MachineId_t, MachineState>& b) {
        MachineInfo_t a_info = Machine_GetInfo(a.first);
        MachineInfo_t b_info = Machine_GetInfo(b.first);
        return a_info.memory_used < b_info.memory_used;
    });

    // Iterate over all machines 
    for (unsigned i = 0; i < sorted_machines.size(); i++) {
        MachineId_t m_id = sorted_machines[i].first;
        MachineState m_state = sorted_machines[i].second;
        // Iterate over all tasks - we can do this b/c only 1 task per VM
        for (auto poss_vm_id : m_state.vms) {
            TaskId_t task_id = VM_GetInfo(poss_vm_id).active_tasks[0];
            TaskInfo_t task = GetTaskInfo(task_id);

            // For each task, try and find a more loaded machine to migrate to
            for (unsigned j = i + 1; j < sorted_machines.size(); j++) {
                MachineId_t target_id = sorted_machines[j].first;
                MachineInfo_t target_machine_info = Machine_GetInfo(target_id);
                unsigned memory_left = target_machine_info.memory_size - target_machine_info.memory_used;
                // If we can migrate this to a more loaded machine, do it
                if (RequiredCPUType(task_id) == Machine_GetCPUType(target_id) && memory_left >= GetTaskMemory(task_id)) {
                
                    SimOutput("Start migration " + to_string(poss_vm_id) + " from " + to_string(m_id) + " to " + to_string(target_id), 1);
                    VM_Migrate(poss_vm_id, target_id);

                    // Update the data structures
                    machine_states[m_id].vms.erase(poss_vm_id);
                    ongoing_migrations[poss_vm_id] = target_id;
                    // We can add the vm to the machines state of target in MigrationCompleted
                    break; 
                } 
            }
        }

        // If this machine is now empty, turn it off
        MachineInfo_t m_info = Machine_GetInfo(m_id);

        // Don't sleep the machine if we are migrating a VM to it currently
        if (m_info.active_vms == 0 && total_on_machines > 1 && !Machine_IsMigrationTarget(m_id)) {
            Machine_SetState(m_id, SLEEP_STATE);
            SimOutput("Scheduler::TaskComplete(): Machine " + to_string(m_id) + " is now empty and is being turned off", 1);
            machine_states[m_id].state = TRANSITION;
            total_on_machines--;
        }

    }
    
    PrintMachineToVMs();
    
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
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 1);
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
    // BUG: The violation comes in same time as completion, but we somehow go ahead with the migration instead of shutting down so 
    // the VM still exists on a different machine with no task?????????
    TaskInfo_t task = GetTaskInfo(task_id);
    SimOutput("SLAWarning(): Got violation for " + to_string(task_id) + " at time " + to_string(time), 1);
    if (task.completion <= time && task.remaining_instructions == 0) {
        return;
    }
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
    SimOutput("StateChangeComplete(): Machine " + to_string(machine_id) + " has completed state change to " + to_string(Machine_GetInfo(machine_id).s_state) + " at time " + to_string(time), 1);
    MachineInfo_t m_info = Machine_GetInfo(machine_id);
    // If machine turned on
    if (m_info.s_state == S0) {
        machine_states[machine_id].state = ON;
        total_on_machines++;
        // Add all pending tasks for it
        if (pending_attachments[machine_id].size() > 0) {
            // We have pending attachments
            for (auto& task_id : pending_attachments[machine_id]) {
                Add_TaskToMachine(machine_id, task_id);
                SimOutput("StateChangeComplete(): Added pending " + to_string(task_id) + " to machine " + to_string(machine_id), 1);
            }
            pending_attachments.erase(machine_id);
        }
        PrintMachineToVMs();
    } else if (m_info.s_state == SLEEP_STATE) {
        // If we turned it off
        machine_states[machine_id].state = OFF; 
        // But we added pending tasks while turning it off, then we need to turn it back on OOPS! Really slow
        // Had to allow adding pending tasks to transitiong machines so it could work. Probably not OK.
        if (pending_attachments[machine_id].size() > 0) {
            Machine_SetState(machine_id, S0);
            machine_states[machine_id].state = TRANSITION;
        }
    }
}