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

MachineState_t SLEEP_STATE = S0i1; // State we initially shut down empty PM to
const int SLEEP_UNIT = 100000000; // (abritrarily chosen) could be diff for diff testcases?
double MIN_MACHINE_PERCENT_IN_STATE = 0.00; // Try 0.3 works except for MatchMeIfYouCan
const bool PROGRESS_BAR = true;
const bool MACHINE_STATE = false;
const bool TEST = false;

// Objective function weights
const unsigned W_STATE = 5; // used to be 7 or 8
const unsigned W_S_STATE = 3;
const unsigned W_CORES = 8;
const unsigned W_MEM = 8;
const unsigned W_GPU = 2;
const unsigned W_PRIORITY = 2;
const unsigned W_PENDING = 4;
const unsigned W_TIME = 0; // lower this if failing SLA?
const unsigned W_MIPS = 2;

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

// BurstTracker by Claude 3.5 to detect bursts so we can change the sleep state for more accurate response times
struct BurstTracker {
    static const Time_t WINDOW_SIZE = 100000;    // 100K time window
    static const int BURST_THRESHOLD = 50;       // Tasks per window to detect burst
    static const int QUIET_THRESHOLD = 10;       // Tasks per window to detect end
    static const int HISTORY_SIZE = 5;           // Track last 5 windows
    static const int QUIET_WINDOWS = 3;          // Number of quiet windows before ending burst
    
    Time_t last_check;
    int task_count;
    bool in_burst;
    vector<int> task_history;
    MachineState_t current_sleep_state;
    int quiet_window_count;
    
    BurstTracker() : last_check(0), task_count(0), in_burst(false), 
                     current_sleep_state(S1), quiet_window_count(0) {
        task_history.resize(HISTORY_SIZE, 0);
    }
    
    bool updateBurstStatus(Time_t now) {
        bool state_changed = false;
        
        // Check if window completed
        if (now - last_check > WINDOW_SIZE) {
            // Shift history
            std::rotate(task_history.begin(), task_history.begin() + 1, task_history.end());
            task_history.back() = task_count;
            
            // Calculate average tasks in recent windows
            double avg_tasks = std::accumulate(task_history.begin(), 
                                             task_history.end(), 
                                             0.0) / HISTORY_SIZE;
            
            if (!in_burst && task_count > BURST_THRESHOLD) {
                // Burst start detected
                in_burst = true;
                current_sleep_state = S0i1;
                quiet_window_count = 0;
                state_changed = true;
                SimOutput("BurstTracker: Burst started. Switching to S0i1", 1);
            }
            else if (in_burst && task_count < QUIET_THRESHOLD) {
                // Count quiet windows
                quiet_window_count++;
                
                if (quiet_window_count >= QUIET_WINDOWS) {
                    // Burst end detected after several quiet windows
                    in_burst = false;
                    current_sleep_state = S1;
                    quiet_window_count = 0;
                    state_changed = true;
                    SimOutput("BurstTracker: Burst ended. Switching back to S1", 1);
                }
            }
            else {
                // Reset quiet window count if we see activity
                quiet_window_count = 0;
            }
            
            // Reset for new window
            task_count = 0;
            last_check = now;
        }
        
        return state_changed;
    }
    
    void recordTask() {
        task_count++;
    }
};
BurstTracker burst_tracker;

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

// Function that checks if the whole system is overloaded (all cores filled, a lot of tasks on each VM...)
bool IsSystemOverloaded() {
    unsigned total_cores = 0;
    unsigned used_cores = 0;
    unsigned total_tasks = 0;
    unsigned total_vms = 0;

    for (const auto& [machine_id, m_state] : machine_states) {
        if (m_state.state != ON) continue; // Only consider machines that are ON

        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        total_cores += machine_info.num_cpus;
        used_cores += machine_info.active_vms;
        
        for (const auto& vm_id : m_state.vms) {
            VMInfo_t vm_info = VM_GetInfo(vm_id);
            total_tasks += vm_info.active_tasks.size();
            total_vms++;
        }
    }

    // Define thresholds for overload
    double core_utilization = (double)used_cores / total_cores;
    double avg_tasks_per_vm = total_vms > 0 ? (double)total_tasks / total_vms : 0.0;
    
    unsigned total_machines = machine_states.size();
    unsigned machines_off = total_machines - total_on_machines;
    double machine_off_ratio = (double)machines_off / total_machines;

    // System is considered overloaded if core utilization exceeds 90% or average tasks per VM exceed 10
    if (machine_off_ratio < 0.5 && (core_utilization > 0.9 || avg_tasks_per_vm > 10.0)) {
        SimOutput("IsSystemOverloaded(): System is overloaded. Core utilization: " + to_string(core_utilization * 100) + "%, Avg tasks per VM: " + to_string(avg_tasks_per_vm), 1);
        return true;
    }

    SimOutput("IsSystemOverloaded(): System is not overloaded. Core utilization: " + to_string(core_utilization * 100) + "%, Avg tasks per VM: " + to_string(avg_tasks_per_vm), 1);
    return false;
}

/*
    Sets the machine P-state by determining load on it (whether that encompasses number of cores filled, memory, number of total tasks, etc.)
*/
void SetMachinePState(MachineId_t machine_id) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    if (machine_info.s_state != S0) return;

    // Calculate load factors
    double core_utilization = (double)machine_info.active_vms / machine_info.num_cpus;
    if (core_utilization == 0.0) {
        Machine_SetCorePerformance(machine_id, -1, P3);
    } else {
        Machine_SetCorePerformance(machine_id, -1, P0);
    }
    SimOutput("SetMachinePState(): Machine " + to_string(machine_id) + " set to P-state " + to_string(Machine_GetInfo(machine_id).p_state), 1);
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
    1. On/off state
    2. Core?
    3. Memory 
    4. GPU
    5. # of pending if OFF
    6. Priority/SLA??? TBD
    7. S State of machine
    8. MIPS
*/
double ComputeMachineScoreForAdd(MachineId_t machine_id, TaskId_t task_id) {
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

    // S state
    double s_state_score;
    if (machine_info.s_state == S0) {
        s_state_score = 1.0;
    } else if (machine_info.s_state == S0i1) {
        s_state_score = 0.88;
    } else if (machine_info.s_state == S1) {
        s_state_score = 0.8;
    } else if (machine_info.s_state == S2) {
        s_state_score = 0.6;
    } else if (machine_info.s_state == S3) {
        s_state_score = 0.4;
    } else if (machine_info.s_state == S4) {
        s_state_score = 0.2;
    } else {
        s_state_score = 0.1;
    }

    // Time since last state change?
    double time_score;
    // If we are on, we don't want this affecting us, if we are off, it matters
    if (machine_state.state == ON) {
        time_score = 1.0;
    } else {
        // If we are off, we want to choose machines that have been off for a shorter amount of time
        double time_since_change = (Now() - machine_state.last_state_change) / 10000.0; 
        time_score = 1.0 / (1.0 + time_since_change); // Higher score for shorter time since last state change
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

    // Priority score
    double priority_score = 0.0;
    Priority_t task_priority = task.priority;

    for (const auto& vm_id : machine_state.vms) {
        VMInfo_t vm_info = VM_GetInfo(vm_id);

        switch (task_priority) {
            case HIGH_PRIORITY:
                // Prefer machines with fewer high-priority tasks
                priority_score += std::count_if(vm_info.active_tasks.begin(), vm_info.active_tasks.end(), [](TaskId_t t) {
                    return GetTaskInfo(t).priority == HIGH_PRIORITY;
                });
                break;

            case MID_PRIORITY:
                // Prefer machines with fewer high and medium-priority tasks
                priority_score += std::count_if(vm_info.active_tasks.begin(), vm_info.active_tasks.end(), [](TaskId_t t) {
                    Priority_t p = GetTaskInfo(t).priority;
                    return p == HIGH_PRIORITY || p == MID_PRIORITY;
                });
                break;

            case LOW_PRIORITY:
                // Prefer machines with fewer total tasks
                priority_score += vm_info.active_tasks.size();
                break;
        }
    }

    // Normalize priority score (lower is better)
    priority_score = 1.0 / (1.0 + priority_score);

    // MIPS score
    double mips_score = (double)machine_info.performance[machine_info.p_state] / 3000.0; // Normalize to max MIPS
    if (task.required_sla == SLA1) {
        // Give SLA1 tasks preference for higher MIPS machines when they're less loaded
        mips_score *= (1.0 - ((double)machine_info.active_vms / machine_info.num_cpus));
    }

    // Calculate final score with weights // (W_TIME * time_score) + Using time may ruin SLA because we shutdown more machines
    double total_score = (W_STATE * state_score) + 
                            (W_S_STATE * s_state_score) +
                            (W_CORES * core_score) + 
                            (W_MEM * mem_score) + 
                            (W_TIME * time_score) +
                            (W_PRIORITY * priority_score) +
                            (W_MIPS * mips_score) +
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
        if (score > best_score) {
            best_machine_id = machine_id;
            best_score = score;
        }
    }
    return best_machine_id;
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
Horrible on time, SLA, and energy due to migrating only being worth if it is a long process, else the extra time taken is useless
- For futher optimization, try load balancing tasks/VMs to consolidate light machines
    a. To start, just sort by least vms, and migrate them to machines with open cores (sort descending machines below max cores)
*/
void LoadBalanceTasks() {
    // Step 1: Identify heavily loaded VMs
    vector<pair<VMId_t, size_t>> heavy_vms;
    for (const auto& [machine_id, m_state] : machine_states) {
        if (m_state.state != ON) continue; // Only consider machines that are ON
        for (const auto& vm_id : m_state.vms) {
            VMInfo_t vm_info = VM_GetInfo(vm_id);
            if (vm_info.active_tasks.size() > 10) { // Threshold for heavily loaded VMs
                heavy_vms.emplace_back(vm_id, vm_info.active_tasks.size());
            }
        }
    }

    // Sort heavily loaded VMs by the number of tasks (descending)
    std::sort(heavy_vms.begin(), heavy_vms.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // Step 2: Identify lightly loaded VMs
    vector<pair<VMId_t, size_t>> light_vms;
    for (const auto& [machine_id, m_state] : machine_states) {
        if (m_state.state != ON) continue; // Only consider machines that are ON
        for (const auto& vm_id : m_state.vms) {
            VMInfo_t vm_info = VM_GetInfo(vm_id);
            if (vm_info.active_tasks.size() < 5) { // Threshold for lightly loaded VMs
                light_vms.emplace_back(vm_id, vm_info.active_tasks.size());
            }
        }
    }

    // Sort lightly loaded VMs by the number of tasks (ascending)
    std::sort(light_vms.begin(), light_vms.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // Step 3: Balance tasks between heavily and lightly loaded VMs
    for (auto& [heavy_vm_id, heavy_task_count] : heavy_vms) {
        VMInfo_t heavy_vm_info = VM_GetInfo(heavy_vm_id);
        vector<TaskId_t> tasks_to_migrate(heavy_vm_info.active_tasks.begin(), heavy_vm_info.active_tasks.end());

        for (auto& [light_vm_id, light_task_count] : light_vms) {
            if (heavy_task_count <= light_task_count + 1) break; // Stop if tasks are balanced

            VMInfo_t light_vm_info = VM_GetInfo(light_vm_id);

            // Check compatibility and migrate tasks
            for (auto task_id : tasks_to_migrate) {
                TaskInfo_t task_info = GetTaskInfo(task_id);

                // Check compatibility (e.g., VM type, CPU type, memory availability)
                bool compatible_vm_type = light_vm_info.vm_type == RequiredVMType(task_id);
                bool compatible_cpu_type = light_vm_info.cpu == RequiredCPUType(task_id);
                MachineInfo_t m_info = Machine_GetInfo(light_vm_info.machine_id);
                bool sufficient_memory = m_info.memory_size - m_info.memory_used >= GetTaskMemory(task_id);

                if (!IsTaskCompleted(task_id) && compatible_vm_type && compatible_cpu_type && sufficient_memory) {
                    // Migrate the task
                    DebugVM(heavy_vm_id);
                    VM_RemoveTask(heavy_vm_id, task_id);
                    VM_AddTask(light_vm_id, task_id, task_info.priority);
                    task_assignments[task_id] = light_vm_id;

                    SimOutput("LoadBalanceTasks(): Migrated task " + to_string(task_id) +
                              " from VM " + to_string(heavy_vm_id) +
                              " to VM " + to_string(light_vm_id), 0);

                    // Update task counts
                    heavy_task_count--;
                    light_task_count++;

                    // Break if tasks are balanced
                    if (heavy_task_count <= light_task_count + 1) break;
                }
            }

            // Break if tasks are balanced
            if (heavy_task_count <= light_task_count + 1) break;
        }
    }
}

// Helper method to bring the least asleep machine back on
MachineId_t ChangeBestMachineState(MachineState_t less_than = S0) {
    for (int state = less_than + 1; state <= S5; ++state) {
        if (state_count[MachineState_t(state)] < 0) continue;
        for (const auto& [machine_id, m_state] : machine_states) {
            if (m_state.state == OFF && Machine_GetInfo(machine_id).s_state == state) {
                state_count[MachineState_t(state)]--;
                Machine_SetState(machine_id, S0);
                machine_states[machine_id].state = TURNING_ON;
                SimOutput("BringLeastSleepMachineOn(): Turning on machine " + to_string(machine_id) + " from state S" + to_string(state), 1);
                return machine_id;
            }
        }
    }
    SimOutput("BringLeastSleepMachineOn(): No machines available to turn on", 1);
    return -1; // No machine found
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

    if (IsSystemOverloaded()) {
        double prev = MIN_MACHINE_PERCENT_IN_STATE;
        MIN_MACHINE_PERCENT_IN_STATE = std::min(0.6, MIN_MACHINE_PERCENT_IN_STATE + 0.5); // Increase by 5%, cap at 50%
        SimOutput("PeriodicCheck(): Increased MIN_MACHINE_PERCENT_IN_STATE to " + to_string(MIN_MACHINE_PERCENT_IN_STATE), 1);
        for (unsigned i = 0; i < int(total_machines * (MIN_MACHINE_PERCENT_IN_STATE-prev)); i++) {
            ChangeBestMachineState();
        } 
    } 
    
    // Turn off PMs that don't have anything on them
    for (auto& [machine_id, m_state] : machine_states) {
        if (m_state.state == OFF || m_state.state == TURNING_OFF) continue;
        if (pending_attachments[machine_id].size() > 0) continue;

        MachineInfo_t m_info = Machine_GetInfo(machine_id);
        if (m_info.active_tasks > 0) continue;
        // If this machine is empty, turn it off
        // Don't sleep the machine if we are migrating a VM to it currently - (int)(total_machines * 0.1)
        if (m_info.active_vms == 0 && total_on_machines > 1 + int(total_machines * MIN_MACHINE_PERCENT_IN_STATE) && !Machine_IsMigrationTarget(machine_id)) {
            state_count[m_info.s_state]--;
            Machine_SetState(machine_id, SLEEP_STATE);
            SimOutput("Scheduler::PeriodicCheck(): Machine " + to_string(machine_id) + " is now empty and is being turned off", 1);
            machine_states[machine_id].state = TURNING_OFF;
            total_on_machines--;
            on_cpu_count[m_info.cpu]--;
        }
    }

    // TBD: Maybe we can track the time since a machine has been off, if its been a long time, lower the S state
    // We need to make sure theres a certain # of machines in S0 or S1 if we want to take machines to lower states
    
    for (auto& [machine_id, m_state] : machine_states) {
        MachineState_t curr_s_state = Machine_GetInfo(machine_id).s_state;
        if (m_state.state != OFF) continue;

        // Calculate the time threshold for transitioning to a lower S state.
        Time_t time_threshold = SLEEP_UNIT * (1 << (Machine_GetInfo(machine_id).s_state - SLEEP_STATE));

        // Define the minimum percentage required for each state dynamically.
        double min_percentage_in_state = MIN_MACHINE_PERCENT_IN_STATE / (1 << (curr_s_state - SLEEP_STATE));

        // Check if the machine has been in its current state longer than the calculated threshold.
        // Ensure the minimum percentage condition is met for the current state.
        if (now - m_state.last_state_change > time_threshold && state_count[curr_s_state] > (int)(total_machines * min_percentage_in_state)) {
            // If it's been a long time, lower the S state
            int new_state = Machine_GetInfo(machine_id).s_state + 1;
            if (new_state > S5) continue;
            state_count[curr_s_state]--;
            Machine_SetState(machine_id, MachineState_t(new_state));
            machine_states[machine_id].state = TURNING_OFF; // - NECESSARY?
        }
    }

    // for (auto& m_id : machines) {
    //     SetMachinePState (m_id);
    // }
    // We will run our load balancing algorithm here
    
    // Check if there are any tasks that are going to violate SLA
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