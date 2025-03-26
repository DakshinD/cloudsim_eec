//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

/*
    TO-DO/Thoughts:
    - Fix starvation problem
    - use burst property again, check if sys overloaded, if it is, increase min percentage, and turn 
      bunch machines on
*/

#include "Scheduler.hpp"
#include <map>
#include <set>
#include <algorithm>
#include <cassert>
#include <numeric>

using std::map;
using std::set;

enum MachinePowerState {
    ON = 0,
    TURNING_ON = 1, 
    TURNING_OFF = 2,
    OFF = 3,
};

struct MachineState { 
    set<VMId_t> vms;
    MachinePowerState state;
    Time_t last_state_change;
};

const bool PROGRESS_BAR = true;
const bool MACHINE_STATE = false;
const bool TEST = false;

// Progress Bar?? Only seen if -v 0
unsigned total_tasks = 0;
unsigned completed_tasks = 0;

// Data Structures for state tracking
unsigned total_machines = -1;
unsigned total_on_machines = -1;
map<CPUType_t, unsigned> on_cpu_count;
map<MachineState_t, unsigned> state_count;

map<TaskId_t, VMId_t> task_assignments;
vector<VMId_t> vms;
map<MachineId_t, MachineState> machine_states;
map<MachineId_t, vector<TaskId_t>> pending_attachments; // We want to add VMs to a machine that is transitioning to ON but isnt ON yet.
map<VMId_t, MachineId_t> ongoing_migrations;

// Reporting Data Structures
int total_sla[NUM_SLAS] = {0};
int sla_violations[NUM_SLAS] = {0};

void DebugVM(VMId_t vm_id) {
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    MachineId_t machine_id = vm_info.machine_id;
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    
    // Count tasks by priority
    int high_priority_count = 0;
    int mid_priority_count = 0;
    int low_priority_count = 0;

    for (const auto& task_id : vm_info.active_tasks) {
        Priority_t priority = GetTaskInfo(task_id).priority;
        if (priority == HIGH_PRIORITY) {
            high_priority_count++;
        } else if (priority == MID_PRIORITY) {
            mid_priority_count++;
        } else if (priority == LOW_PRIORITY) {
            low_priority_count++;
        }
    }

    string output = "VM Details:\n";
    output += "----------------------------------------\n";
    output += "\033[1;35mVM " + to_string(vm_id) + " on Machine " + to_string(machine_id) + "\033[0m\n";
    output += "Machine State: \033[1;33m" + 
             string(machine_states[machine_id].state == ON ? "ON" : 
                   machine_states[machine_id].state == TURNING_ON ? "TURNING_ON" : 
                   machine_states[machine_id].state == TURNING_OFF ? "TURNING_OFF" : "OFF") + 
             "\033[0m\n";
    output += "Tasks by Priority: [\033[1;31m" + to_string(high_priority_count) + // Red for high
             "\033[0m, \033[1;33m" + to_string(mid_priority_count) +              // Yellow for mid
             "\033[0m, \033[1;32m" + to_string(low_priority_count) +              // Green for low
             "\033[0m]\n";
    SimOutput(output, 0);
}

void Debug() {
    string res = "DETAILED MACHINE TO VMs BREAKDOWN:\n";
    for (const auto& [machine_id, m_state] : machine_states) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        if (machine_info.active_tasks == 0 && pending_attachments[machine_id].size() == 0) continue;
        // if (m_state.vms.size() != machine_info.active_vms) ThrowException("Machine " + to_string(machine_id) + " has " + to_string(m_state.vms.size()) + " VMs but " + to_string(machine_info.active_vms) + " active VMs");
        res += "\033[1;35mMachine " + to_string(machine_id) + " (" + to_string(m_state.vms.size()) + " VMs / " + to_string(machine_info.num_cpus) + " CPUs):\033[0m (" + "\033[1;34m" + to_string(pending_attachments[machine_id].size()) + "\033[0m) " +
               "[\033[1;36mS-State: " + to_string(machine_info.s_state) + "\033[0m, \033[1;33mPower-State: " + (m_state.state == ON ? "ON" : m_state.state == TURNING_ON ? "TURNING_ON" : m_state.state == TURNING_OFF ? "TURNING_OFF" : "OFF") + "\033[0m]\n";
        for (const auto& vm_id : m_state.vms) {
            VMInfo_t vm_info = VM_GetInfo(vm_id);
            int high_priority_count = 0;
            int mid_priority_count = 0;
            int low_priority_count = 0;

            for (const auto& task_id : vm_info.active_tasks) {
                Priority_t priority = GetTaskInfo(task_id).priority;
                if (priority == HIGH_PRIORITY) {
                    high_priority_count++;
                } else if (priority == MID_PRIORITY) {
                    mid_priority_count++;
                } else if (priority == LOW_PRIORITY) {
                    low_priority_count++;
                }
            }

            res += "  VM " + to_string(vm_id) + ": [\033[1;31m" + to_string(high_priority_count) + "\033[0m, \033[1;33m" + to_string(mid_priority_count) + "\033[0m, \033[1;32m" + to_string(low_priority_count) + "\033[0m]\n";
        }
    }
    SimOutput(res, 0);
}


/*
    We need to choose a VM based on priority, but also CPU type and GPU, have a VM have same CPU/GPU type 
*/
void Add_TaskToMachine(MachineId_t machine_id, TaskId_t task_id) {
    // Choose whether to create VM or add to existing
    VMId_t vm_id;
    bool created_new_vm = false;
    TaskInfo_t task_info = GetTaskInfo(task_id);
    Priority_t task_priority = task_info.priority;

    /* 
        1. Create a new VM when
            a. # of VMs < # of CPUs
            b. Load balancing 
        2. Add to existing VM
            a. # of VMs >= # of CPUs
    */

    // If # of VMs < # of CPUs, create a new VM
    if (machine_states[machine_id].vms.size() < Machine_GetInfo(machine_id).num_cpus) {
        vm_id = VM_Create(RequiredVMType(task_id), RequiredCPUType(task_id));
        created_new_vm = true;
    } else {
        // Find an existing VM to add the task to TBD: Needs to be the required VM Type
        VMId_t best_vm_id = -1;
        double best_score = __DBL_MAX__;
        for (const auto& vm : machine_states[machine_id].vms) {
            VMInfo_t vm_info = VM_GetInfo(vm);
            if (RequiredVMType(task_id) != vm_info.vm_type) continue;

            double score = 0.0;
            switch (task_priority) {
                case HIGH_PRIORITY:
                    // For high priority, find VM with least high and mid priority tasks
                    score += std::count_if(vm_info.active_tasks.begin(), vm_info.active_tasks.end(), [](TaskId_t t) {
                        Priority_t p = GetTaskInfo(t).priority;
                        return p == HIGH_PRIORITY;
                    });
                    break;
                case MID_PRIORITY:
                    // For medium priority, find VM with least amount of high + medium priority tasks
                    score += std::count_if(vm_info.active_tasks.begin(), vm_info.active_tasks.end(), [](TaskId_t t) {
                        Priority_t p = GetTaskInfo(t).priority;
                        return p == HIGH_PRIORITY || p == MID_PRIORITY;
                    });
                    
                    break;
                case LOW_PRIORITY:
                    // For low priority, find VM with least total tasks
                    score = vm_info.active_tasks.size();
                    break;
            }
            // Update our best vm
            if (score < best_score) {
                best_score = score;
                best_vm_id = vm;
            }
        }
        if (best_vm_id != -1) {
            vm_id = best_vm_id;
        } else {
            // If no suitable VM found, create a new one
            vm_id = VM_Create(RequiredVMType(task_id), RequiredCPUType(task_id));
            created_new_vm = true;
        }
    }
    if (created_new_vm) {
        VM_Attach(vm_id, machine_id);
        int prev_sz = machine_states[machine_id].vms.size();
        machine_states[machine_id].vms.insert(vm_id);
        assert(prev_sz + 1 == machine_states[machine_id].vms.size());
    } 
    VM_AddTask(vm_id, task_id, Priority_t(GetTaskPriority(task_id)));
    task_assignments[task_id] = vm_id;
    SimOutput("NewTask(): Added " + to_string(task_id) + " on vm: " + to_string(vm_id) + " to on machine " + to_string(machine_id), 1);

    return;
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
        machine_states[i] = {{}, ON, Now()};
        on_cpu_count[Machine_GetCPUType(MachineId_t(i))]++;
        state_count[S0]++;
        // Set the default state of all machines to sleeping for greedy
        // Machine_SetState (MachineId_t(i), S0);
        SimOutput("Scheduler::Init(): Created machine id of " + to_string(i), 4);
    }    

    SLEEP_STATE = burst_tracker.current_sleep_state;
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

/*
Use objective function to find machine and add it according to machine state (ON/OFF)
*/
void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    burst_tracker.recordTask();
    if (burst_tracker.updateBurstStatus(now)) {
        // get the new sleep state from burst tracker
        SLEEP_STATE = burst_tracker.current_sleep_state;
        
        // log change
        string message = burst_tracker.in_burst ? 
            "Burst started! Changing sleep state to S0i1" :
            "Burst ended! Changing sleep state back to S1";
        SimOutput(message + " (Task count: " + 
                 to_string(burst_tracker.task_count) + ")", 0);

        for (auto& [machine_id, m_state] : machine_states) {
            if (m_state.state == OFF) {
                MachineInfo_t m_info = Machine_GetInfo(machine_id);
                if (m_info.s_state > SLEEP_STATE) {
                    state_count[m_info.s_state]--;
                    Machine_SetState(machine_id, SLEEP_STATE);
                    machine_states[machine_id].state = TURNING_OFF; 
                }
            }
        }
    }

    SimOutput("NewTask(): New task at time: " + to_string(now), 1);
    // Get the task parameters
    TaskInfo_t task = GetTaskInfo (task_id);

    // Set the task priority based on its SLA
    // if (task.required_sla == SLA0) {
    //     SetTaskPriority(task_id, HIGH_PRIORITY);
    // } else if (task.required_sla == SLA1 || task.required_sla == SLA2) {
    //     SetTaskPriority(task_id, MID_PRIORITY);
    // } else if (task.required_sla == SLA3) {
    //     SetTaskPriority(task_id, LOW_PRIORITY);
    // }

    // Keep track of best matched machine
    MachineId_t best_machine_id = GetBestScoreMachine(task_id); 

    if (best_machine_id == -1) {
        ThrowException("Scheduler::NewTask(): Couldn't find a machine for task " + to_string(task_id));
    }

    // We have a machine, lets check its state
    MachineState target = machine_states[best_machine_id];
    if (target.state == ON) {
        Add_TaskToMachine(best_machine_id, task_id);
    }
    else if (target.state == OFF || target.state == TURNING_ON) {
        if (target.state == OFF) {
            state_count[Machine_GetInfo(best_machine_id).s_state]--;
            Machine_SetState(best_machine_id, S0);
            machine_states[best_machine_id].state = TURNING_ON;
        }
        pending_attachments[best_machine_id].push_back(task_id);
        SimOutput("NewTask(): Added PENDING " + to_string(task_id) + " to off machine " + to_string(best_machine_id), 1); 
    } else {
        // state == TURNING_OFF
        // Question: What happens if we setstate to S0 while it is turning off?
        pending_attachments[best_machine_id].push_back(task_id);
        SimOutput("NewTask(): Added PENDING " + to_string(task_id) + " to turning off machine " + to_string(best_machine_id), 1); 
    }
    
    return;
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
    std::cout << "] " << int(progress * 100.0) << " % - " << int(completed_tasks) << "/" << int(total_tasks) << " and on_machines: " << total_on_machines << "\r";
    std::cout.flush();
}

void DisplayMachineStates() {
    int state_counts[7] = {0}; // S0 to S5
    for (const auto& [machine_id, m_state] : machine_states) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        state_counts[int(machine_info.s_state)]++;
    }
    std::cout << "Machine States: ";
    for (int i = 0; i <= 6; i++) {
        if (i == 1) std::cout << "S0i1: " << state_counts[i];
        else{
            std::cout << "S" << max(0, i-1) << ": " << state_counts[i];
        }
        
        if (i < S5) std::cout << ", ";
    }
    std::cout << "\r";
    std::cout.flush();
}

void MigrateHelper(VMId_t vm_id, MachineId_t start_m, MachineId_t end_m) {
    SimOutput("Start migration " + to_string(vm_id) + " from " + to_string(start_m) + " to " + to_string(end_m), 1);
    VM_Migrate(vm_id, end_m);

    // Update the data structures
    machine_states[start_m].vms.erase(vm_id);
    ongoing_migrations[vm_id] = end_m;
}


/*
    Idea: We could track the last instruction we were at for that task, and then if it seems that we aren't on track for completion,
    load balance it or up the priority
*/
void Scheduler::PeriodicCheck(Time_t now) {
    if (PROGRESS_BAR) {
        DisplayProgressBar();
    }
    if (MACHINE_STATE) {
        DisplayMachineStates();
    }
}

void Scheduler::Shutdown(Time_t time) {
    for (auto id : vms) {
        VM_Shutdown(id);
    }

    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    VMId_t vm_id = task_assignments[task_id];
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now) + " on vm " + to_string(vm_id), 1);
    
    // if (VM_GetInfo(vm_id).active_tasks.size() != 0) {
    //     ThrowException("Somehow there are active tasks on a VM?????");
    // }

    // PROGRESS
    completed_tasks++;
    total_sla[RequiredSLA(task_id)]++;

    // First, delete the task
    task_assignments.erase(task_id);
    
    MachineId_t orig_m_id = vm_info.machine_id;

    // The problem is trying to shut down a VM that is going through migration
    if (!ongoing_migrations.count(vm_id) && vm_info.active_tasks.size() == 0) {
        VM_Shutdown(vm_id);
        machine_states[orig_m_id].vms.erase(vm_id);
    }
    assert(machine_states[orig_m_id].vms.size() == Machine_GetInfo(orig_m_id).active_vms); 
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

/*
    Realistically with our objective function, memory overloads will never happen or will be minimized if 
    the weights are tuned properly.
*/
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
    cout << "Detailed SLA Violation Report" << endl;
    for (unsigned i = 0; i < NUM_SLAS; i++) {
        if (total_sla[i] == 0) continue;

        double violation_percentage = ((double)(sla_violations[i]) / total_sla[i]) * 100;
        double compliance_percentage = 100.0 - violation_percentage;

        // Determine if the SLA passed or failed
        bool sla_passed = false;
        double required_compliance = 0.0;
        if (i == SLA0) {
            required_compliance = 95.0;
            if (compliance_percentage >= required_compliance) sla_passed = true;
        } else if (i == SLA1) {
            required_compliance = 90.0;
            if (compliance_percentage >= required_compliance) sla_passed = true;
        } else if (i == SLA2) {
            required_compliance = 80.0;
            if (compliance_percentage >= required_compliance) sla_passed = true;
        } else if (i == SLA3) {
            sla_passed = true; // SLA3 is best effort, always passes
        }

        // Output in green if passed, red if failed
        if (sla_passed) {
            cout << "\033[1;32m"; // Green text
        } else {
            cout << "\033[1;31m"; // Red text
        }

        cout << "SLA" << i << ": Violations = " << sla_violations[i] 
             << "/" << total_sla[i] 
             << " (" << violation_percentage << "% violations, " 
             << compliance_percentage << "% compliance)" 
             << " \033[1;35m[Required: " << required_compliance << "% compliance]\033[0m" << endl;

        cout << "\033[0m"; // Reset text color
    }

    // This function is called before the simulation terminates Add whatever you feel like.
    // cout << "SLA violation report" << endl;
    // cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    // cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    // cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;     // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    
    Scheduler.Shutdown(time);
}

/*
    We do our best on SLA as well so this might not be necessary other than harder testcases?
*/
void SLAWarning(Time_t time, TaskId_t task_id) {
    TaskInfo_t task = GetTaskInfo(task_id);
    sla_violations[task.required_sla]++;
    SimOutput("SLAWarning(): Got violation for " + to_string(task_id) + " at time " + to_string(time), 1);
    if (TEST) {
        Debug();
        ThrowException("SLA Violation for task " + to_string(task_id) + " on machine " + to_string(VM_GetInfo(task_assignments[task_id]).machine_id) + " at time " + to_string(time));
    }

    // What do we do if we identify tasks on the same machine that are going to violate SLA?
    // Starvation is occurring of SLA1 tasks because SLA0 is so spread out
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
    SimOutput("StateChangeComplete(): Machine " + to_string(machine_id) + " has completed state change to " + to_string(Machine_GetInfo(machine_id).s_state) + " at time " + to_string(time), 1);
    MachineInfo_t m_info = Machine_GetInfo(machine_id);
    machine_states[machine_id].last_state_change = time;
    state_count[m_info.s_state]++;

    // If machine turned on
    if (m_info.s_state == S0) {
        machine_states[machine_id].state = ON;
        total_on_machines++;
        on_cpu_count[Machine_GetCPUType(machine_id)]++;

        // Add all pending tasks for it
        if (pending_attachments[machine_id].size() > 0) {
            // We have pending attachments
            for (auto& task_id : pending_attachments[machine_id]) {
                Add_TaskToMachine(machine_id, task_id);
                SimOutput("StateChangeComplete(): Added pending " + to_string(task_id) + " to machine " + to_string(machine_id), 1);
            }
            pending_attachments.erase(machine_id);
        }
        // Debug();
    } else {
        // If we turned it off
        machine_states[machine_id].state = OFF; 
        // But we added pending tasks while turning it off, then we need to turn it back on OOPS! Really slow
        // Had to allow adding pending tasks to transitiong machines so it could work. Probably not OK.
        if (pending_attachments[machine_id].size() > 0) {
            state_count[m_info.s_state]--;
            Machine_SetState(machine_id, S0);
            machine_states[machine_id].state = TURNING_ON;
        }
    }
}