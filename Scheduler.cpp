//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

/*
    TO-DO: going all the way to S2 is too slow for Hour.md, we need something in the middle or things in standby
    - Try and implement adding multiple tasks to VM
    - implement SLA checking in PeriodicCheck
    - try and do load balacning in MemoryWarning
    Do these 3 incrementally with Hour.md and try and get SLA working, then worry abt energy efficiency

*/

#include "Scheduler.hpp"
#include <map>
#include <set>
#include <algorithm>

using std::map;
using std::set;

enum MachinePowerState {
    ON = 0,
    TURNING_ON = 1, 
    TURNING_OFF = 2,
    OFF = 2,
};

struct MachineState { 
    set<VMId_t> vms;
    unsigned memory_used; // not used currently
    MachinePowerState state;
};
// Progress Bar?? Only seen if -v 0
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

// Reporting Data Structures
int total_sla[NUM_SLAS] = {0};
int sla_violations[NUM_SLAS] = {0};


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
        // Find an existing VM to add the task to
        VMId_t best_vm_id = -1;
        double best_score = __DBL_MAX__;
        for (const auto& vm : machine_states[machine_id].vms) {
            VMInfo_t vm_info = VM_GetInfo(vm);
            double score = 0.0;
            switch (task_priority) {
                case HIGH_PRIORITY:
                    // For high priority, find VM with least high priority tasks
                    score = std::count_if(vm_info.active_tasks.begin(), vm_info.active_tasks.end(), [](TaskId_t t) {
                        return GetTaskInfo(t).priority == HIGH_PRIORITY ;
                    });
                    break;
                case MID_PRIORITY:
                    // For medium priority, find VM with least amount of high + medium priority tasks
                    score = std::count_if(vm_info.active_tasks.begin(), vm_info.active_tasks.end(), [](TaskId_t t) {
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
        machine_states[machine_id].vms.insert(vm_id);
    }
    VM_AddTask(vm_id, task_id, Priority_t(GetTaskPriority(task_id)));
    task_assignments[task_id] = vm_id;
    SimOutput("NewTask(): Added " + to_string(task_id) + " on vm: " + to_string(vm_id) + " to on machine " + to_string(machine_id), 1);
    return;
}

void PrintMachineToVMs() {
    string res = "DETAILED MACHINE TO VMs BREAKDOWN:\n";
    for (const auto& [machine_id, m_state] : machine_states) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        if (machine_info.active_tasks == 0) continue;
        res += "\033[1;35mMachine " + to_string(machine_id) + " (" + to_string(m_state.vms.size()) + " VMs / " + to_string(machine_info.num_cpus) + " CPUs):\033[0m\n";
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
        machine_states[i] = {{}, 0, ON};
        // Set the default state of all machines to sleeping for greedy
        // Machine_SetState (MachineId_t(i), S0);
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


/*
    1. On/off state
    2. Core?
    3. Memory 
    4. GPU
    5. # of pending if OFF
    6. Priority/SLA??? TBD
*/
double ComputeMachineScoreForAdd(MachineId_t machine_id, TaskId_t task_id) {
    const unsigned W_STATE = 7;
    const unsigned W_CORES = 8;
    const unsigned W_MEM = 8;
    const unsigned W_GPU = 2;
    const unsigned W_PENDING = 4;

    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    MachineState machine_state = machine_states[machine_id];
    TaskInfo_t task = GetTaskInfo(task_id);

    // On/off state 
    double state_score;
    if (machine_state.state == ON) { // Change this to use S states for more specificity later
        state_score = 1.0;
    } else if (machine_state.state == TURNING_ON) {
        state_score = 0.7;
    } else if (machine_state.state == OFF) {
        state_score = 0.5;
    } else {
        state_score = 0.2;
    }

    // CPU cores
    double core_score;
    if (machine_info.active_vms >= machine_info.num_cpus) {
        core_score = 0.0;
    } else {
        // We weight already loaded machines higher? How is this different from greedy?
        core_score = (double)machine_info.active_vms / machine_info.num_cpus;
    }

    // Memory 
    double mem_score = 1.0 - (double)machine_info.memory_used / machine_info.memory_size;
    if (mem_score < 0) mem_score = 0.0;

    // GPU
    double gpu_score = (machine_info.gpus && task.gpu_capable) ? 1.0 : 0.0;

    // Calcualte final score with weights
    double total_score = (W_STATE * state_score) + 
                            (W_CORES * core_score) + 
                            (W_MEM * mem_score) + 
                            (W_GPU * gpu_score) -
                            (W_PENDING * (double)pending_attachments[machine_id].size());
    return total_score;
}

/*
    Can we turn this for a VM, its not like we are only migrating 1 task? 
    So 
*/
MachineId_t GetBestScoreMachine(TaskId_t task_id) {
    // Iterate over all machines
    double best_score = -__DBL_MAX__;
    MachineId_t best_machine_id = -1;
    for (const auto& [machine_id, m_state] : machine_states) {
        // Don't acknowledge machines without CPU compatibility
        if (RequiredCPUType(task_id) != Machine_GetCPUType(machine_id)) 
            continue;
        // Calc score
        double score = ComputeMachineScoreForAdd(machine_id, task_id);
        // cout << "score: " << score << endl;
        if (score > best_score) {
            best_machine_id = machine_id;
            best_score = score;
        }
    }
    return best_machine_id;
}
/*
 We add this task by starting a new VM on a running machine
       a. Make sure that # of VMs < # of Cores for optimal performance (priority)
       b. Make sure to set priority based on SLA (future, balance high priority across machines?)
       c. Can we satisfy memory requirements
       d. We should choose a machine that was already on, we don't want to
           start a new machine for a single VM
               (i). If we do start a new machine for this VM, (priority reasons?)
                   We want to load balance the tasks from other machines to this
                   new machine depending on number of cores? (Trade offs)

    Can we make an objective function to rank all the machines? Rank:
    - Disqualified if CPU doesnt match
    1. On/off state
    2. Core?
    3. Memory 
    4. GPU
    5. Priority/SLA??? TBD
    
*/
void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    SimOutput("NewTask(): New task at time: " + to_string(now), 1);
    // Get the task parameters
    TaskInfo_t task = GetTaskInfo (task_id);

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
    PrintMachineToVMs();
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
    std::cout << "] " << int(progress * 100.0) << " % - " << int(completed_tasks) << "/" << int(total_tasks) << "\r";
    std::cout.flush();
}

/*
    Idea: We could track the last instruction we were at for that task, and then if it seems that we aren't on track for completion,
    load balance it or up the priority
*/
void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
    DisplayProgressBar();
    
    // We will run our load balancing algorithm here
    
    // Check if there are any tasks that are going to violate SLA
}

/* 
    The taskID is not used, just to match the function pointer so we can pass it into 
    GetBestScoreMachine
*/
double MachineUtilizationScore(MachineId_t machine_id, TaskId_t task_id) {
    const unsigned W_UTIL = 5;
    const unsigned W_MEM = 3;

    MachineInfo_t m_info = Machine_GetInfo(machine_id);

    // CPU util
    double utilization_score = (double)m_info.active_vms / m_info.num_cpus;
    // Mem util
    double mem_score = (double)m_info.memory_used / m_info.memory_size;
    
    double final_score = (W_UTIL * utilization_score) + (W_MEM * mem_score);
    double normalized_score = final_score / (W_UTIL + W_MEM);

    return normalized_score;
}
// vector<double> util_scores;
void LoadBalance() {
    const double UNDERUTILIZED_THRESHOLD = 0.1;
    vector<MachineId_t> underutilized_machines;

    // Identify underutilized machines
    for (auto& [machine_id, m_state] : machine_states) {
        if (m_state.state != ON || m_state.vms.size() == 0) continue;

        MachineInfo_t m_info = Machine_GetInfo(machine_id);
        double utilization_score = MachineUtilizationScore(machine_id, -1);
        // util_scores.push_back(utilization_score);
        if (utilization_score < UNDERUTILIZED_THRESHOLD) {
            // SimOutput("LoadBalance(): Machine " + to_string(machine_id) + " is underutilized with score: " + to_string(utilization_score) + "with active tasks: " + to_string(m_state.vms.size()), 0);
            underutilized_machines.push_back(machine_id);
        }
    }

    // Attempt migrations from underutilized machines - TBD: sort by util ascending
    for (auto& machine_id : underutilized_machines) {
        MachineInfo_t m_info = Machine_GetInfo(machine_id);
        // Check if this machine has any VMs
        if (m_info.active_vms == 0) continue;

        // Iterate over all VMs on this machine
        for (VMId_t vm_id : machine_states[machine_id].vms) {
            // Maybe only migrate if SLA is not 0
            VMInfo_t current_vm = VM_GetInfo(vm_id);
            MachineId_t target_machine_id = GetBestScoreMachine(current_vm.active_tasks[0]); // Also assumes the task has same CPU type
            if (target_machine_id == machine_id) {
                // printf("HERE\n");
                SimOutput("Target machine for migration is itself\n", 1);
                continue;
            }
            else if (target_machine_id == -1) {
                // printf("HERE\n");
                SimOutput("No target machine for migration\n", 1);
                continue;
            }
            // // Now migrate
            // SimOutput("Start migration " + to_string(vm_id) + " from " + to_string(machine_id) + " to " + to_string(target_machine_id), 0);
            // VM_Migrate(vm_id, target_machine_id);

            // // Update the data structures
            // machine_states[machine_id].vms.erase(vm_id);
            // ongoing_migrations[vm_id] = target_machine_id;
        }
        
        // If this machine is empty, turn it off
        // Don't sleep the machine if we are migrating a VM to it currently
        if (m_info.active_vms == 0 && total_on_machines > 1 && !Machine_IsMigrationTarget(machine_id)) {
            Machine_SetState(machine_id, S2);
            SimOutput("Scheduler::TaskComplete(): Machine " + to_string(machine_id) + " is now empty and is being turned off", 1);
            machine_states[machine_id].state = TURNING_OFF;
            total_on_machines--;
        }
    }
}

void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for (auto id : vms) {
        VM_Shutdown(id);
    }
    // std::cout << "Utilization Score Distribution:" << std::endl;
    // for (double score : util_scores) {
    //     std::cout << score << std::endl;
    // }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void MigrateHelper(VMId_t vm_id, MachineId_t start_m, MachineId_t end_m) {
    SimOutput("Start migration " + to_string(vm_id) + " from " + to_string(start_m) + " to " + to_string(end_m), 1);
    VM_Migrate(vm_id, end_m);

    // Update the data structures
    machine_states[start_m].vms.erase(vm_id);
    ongoing_migrations[vm_id] = end_m;

    // Code for shutting down a VM
   /*
        // Don't sleep the machine if we are migrating a VM to it currently
        if (m_info.active_vms == 0 && total_on_machines > 1 && !Machine_IsMigrationTarget(m_id)) {
            Machine_SetState(m_id, S2);
            SimOutput("Scheduler::TaskComplete(): Machine " + to_string(m_id) + " is now empty and is being turned off", 1);
            machine_states[m_id].state = TURNING_OFF;
            total_on_machines--;
        }
   */ 
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy 

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
    // Delete the VM - only do this because there was only one task
    MachineId_t orig_m_id = vm_info.machine_id;
    machine_states[orig_m_id].vms.erase(vm_id);

    // The problem is trying to shut down a VM that is going through migration
    if (!ongoing_migrations.count(vm_id) && vm_info.active_tasks.size() == 0) {
        VM_Shutdown(vm_id);
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

void SLAWarning(Time_t time, TaskId_t task_id) {
    // BUG: The violation comes in same time as completion, but we somehow go ahead with the migration instead of shutting down so 
    // the VM still exists on a different machine with no task?????????
    TaskInfo_t task = GetTaskInfo(task_id);
    sla_violations[task.required_sla]++;
    SimOutput("SLAWarning(): Got violation for " + to_string(task_id) + " at time " + to_string(time), 1);
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
    } else if (m_info.s_state == S2) {
        // If we turned it off
        machine_states[machine_id].state = OFF; 
        // But we added pending tasks while turning it off, then we need to turn it back on OOPS! Really slow
        // Had to allow adding pending tasks to transitiong machines so it could work. Probably not OK.
        if (pending_attachments[machine_id].size() > 0) {
            Machine_SetState(machine_id, S0);
            machine_states[machine_id].state = TURNING_ON;
        }
    }
}