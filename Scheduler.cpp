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

const MachineState_t SLEEP_STATE = S0i1;
const bool PROGRESS_BAR = true;
const bool MACHINE_STATE = false;
const bool TEST = false;
const bool ENERGY_BAR = false;

// Progress Bar?? Only seen if -v 0
unsigned total_tasks = 0;
unsigned completed_tasks = 0;

// Data Structures for state tracking
unsigned total_machines = -1;
unsigned total_on_machines = -1;
map<CPUType_t, unsigned> on_cpu_count;
map<MachineState_t, unsigned> state_count;

vector<MachineId_t> machines;
map<TaskId_t, VMId_t> task_assignments;
vector<VMId_t> vms;
map<MachineId_t, MachineState> machine_states;
map<MachineId_t, vector<TaskId_t>> pending_attachments; // We want to add VMs to a machine that is transitioning to ON but isnt ON yet.
map<VMId_t, MachineId_t> ongoing_migrations;

// Reporting Data Structures
int total_sla[NUM_SLAS] = {0};
int sla_violations[NUM_SLAS] = {0};

// Energy DS
struct EnergyState {
    double energy_consumption_rate;
    uint64_t prev_energy_consumption;
    Time_t prev_time;
};

map<MachineId_t, EnergyState> machine_energy_rates;
double UNDERUTIL_THRESHOLD = 0.1;
double OVERUTIL_THRESHOLD = 100.0;


// Func headers
MachineId_t GetBestMachine(TaskId_t task_id);
void MigrateHelper(VMId_t vm_id, MachineId_t start_m, MachineId_t end_m);
void Debug();
void DebugVM(VMId_t vm_id);
/*
Implementation from https://www.sciencedirect.com/science/article/pii/S0167739X11000689
"Energy-aware resource allocation heuristics for efficient management of data centers for Cloud computing"
---------
NewTask - 4.1 VM Placement

- Each Task going on its own VM? Seems to be the case
- Calculate current rate of energy consumption for each machine
    a. Keep DS that stores for each machine, prev energy consumption, prev rate, and prev time
    b. In PeriodicCheck, calculate the new rate and update prev energy and prev rate

    

Migration Policy (Also done in PeriodicCheck)
- We set a lower and upper threshold for under/overutilized machines
- We need a method to calculate utilization (cores, tasks, VMs, memory)
1. Underutilized
- Migrate all VMs off
2. Overutilized
- while the current machine is above the threshold
    a. migrate off the VM with lowest priority task
    b. in future, combine remaining instructions, duration left, and priority to find optimal machine to migrate 
*/

/*
    We need a method to calculate utilization (cores, tasks, VMs, memory)
*/
double MachineUtilization (MachineId_t machine_id) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);

    const double W_CORE = 3.0;
    const double W_MEM = 1.0;

    // Calculate utilization based on cores, tasks, VMs, and memory
    double core_utilization = (double)machine_info.active_vms / machine_info.num_cpus;
    double memory_utilization = (double)machine_info.memory_used / machine_info.memory_size;
    
    // Combine the utilization metrics into a single score
    double utilization_score = ((W_CORE * core_utilization) + (W_MEM * memory_utilization)) / 2.0;

    // Normalize the score to a range of 0 to 1
    return utilization_score;
}

/* Implements the migration policy of the paper with under/overutilized thresholds 
    - We had to settle for a priority-based policy for overutilized machines because we don't have a 
    common way to calculate the machine and VM utilization together like the paper does.
*/
void LoadBalance() {
    for (auto& [machine_id, m_state] : machine_states) {
        if (m_state.state != ON) continue;

        MachineInfo_t m_info = Machine_GetInfo (machine_id);
        double machine_util = MachineUtilization (machine_id);

        // Under
        if (machine_util < UNDERUTIL_THRESHOLD) {
            // SimOutput("I am UNDER\n", 0);
            // We need to migrate all VMs off this machine
            assert(m_state.vms.size() == m_info.active_vms);

            set<VMId_t> need_to_migrate = m_state.vms;
            for (const auto& vm_id : need_to_migrate) {
                // Get best machine
                MachineId_t dest_machine = GetBestMachine(VM_GetInfo(vm_id).active_tasks[0]); // Can only do this because there is 1 task per VM
                if (dest_machine == machine_id) {
                    // ThrowException("Migrating to same machine");
                    continue; // NEED TO FIX
                }

                // If we couldn't find an ON machine to migrate to, just give up
                if (machine_states[dest_machine].state != ON) {
                    break;
                }

                MigrateHelper(vm_id, machine_id, dest_machine); 
            }
            m_info = Machine_GetInfo(machine_id); 
            

            // See if we can turn this machine off
            if (m_info.active_vms == 0 && total_on_machines > int(total_machines * 0.5)) { // Keep at least 2 machines of each CPU type on
                Machine_SetState (machine_id, SLEEP_STATE);
                m_state.state = TURNING_OFF;
                total_on_machines--;
                on_cpu_count[Machine_GetCPUType(machine_id)]--;
            }
        }
        
        // Over
        if (machine_util > OVERUTIL_THRESHOLD) {
            
            // SimOutput("I am OVER\n", 0);
            vector<pair<VMId_t, Priority_t>> vm_priorities;

            // calculate utilizations for all VMs on machine
            for (auto& vm_id : m_state.vms) {
                VMInfo_t vm_info = VM_GetInfo(vm_id);
                TaskId_t task_id = vm_info.active_tasks[0];
                TaskInfo_t task_info = GetTaskInfo(task_id);
                vm_priorities.emplace_back(vm_id, task_info.priority);
            }

            sort(vm_priorities.begin(), vm_priorities.end(), [](const auto& a, const auto& b) {
                return a.second < b.second;
            });

            // Migrate VMs with lowest utilization first
            for (auto& [vm_id, vm_util] : vm_priorities) {
                VMInfo_t vm_info = VM_GetInfo(vm_id);
                TaskId_t task_id = vm_info.active_tasks[0];

                MachineId_t dest_machine = GetBestMachine(task_id);
                if (dest_machine == machine_id || dest_machine == -1) continue;
                
                MigrateHelper(vm_id, machine_id, dest_machine);
                machine_util = MachineUtilization(machine_id);

                if (machine_util <= OVERUTIL_THRESHOLD) break;
            }
        }
    }
}

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
        if (machine_info.active_tasks == 0 && machine_info.active_vms == 0 && pending_attachments[machine_id].size() == 0) continue;
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

void DebugMachineEnergy() {
    string res = "MACHINE ENERGY AND TASK BREAKDOWN:\n";
    res += "----------------------------------\n";
    
    for (const auto& [machine_id, m_state] : machine_states) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        
        // Skip empty idle machines
        if (machine_info.active_tasks == 0 && 
            machine_info.active_vms == 0 && 
            pending_attachments[machine_id].size() == 0) {
            continue;
        }
        
        // Format energy rate with color based on consumption
        double energy_rate = machine_info.energy_consumed - machine_energy_rates[machine_id].prev_energy_consumption;
        string energy_color = energy_rate > 10.0 ? "\033[1;31m" :  // Red for high
                             energy_rate > 5.0 ? "\033[1;33m" :     // Yellow for medium
                             "\033[1;32m";                          // Green for low
        
        res += "Machine " + to_string(machine_id) + 
               " [\033[1;36mTasks: " + to_string(machine_info.active_tasks) + 
               "\033[0m, Energy Rate: " + energy_color + 
               to_string(energy_rate) + " KW/h\033[0m, State: \033[1;35m" +
               (m_state.state == ON ? "ON" : 
                m_state.state == TURNING_ON ? "TURNING_ON" : 
                m_state.state == TURNING_OFF ? "TURNING_OFF" : "OFF") +
               "\033[0m]\n";
    }
    
    SimOutput(res, 0);
}

/*
    Add task to an on or off machine
*/
void Add_TaskToMachine(MachineId_t machine_id, TaskId_t task_id) {
    // If the machine is ON
    if (machine_states[machine_id].state == ON) {
        VMId_t vm_id = VM_Create(RequiredVMType(task_id), RequiredCPUType(task_id));
        TaskInfo_t task_info = GetTaskInfo(task_id);
        Priority_t task_priority = task_info.priority;

        VM_Attach(vm_id, machine_id);
        machine_states[machine_id].vms.insert(vm_id);

        VM_AddTask(vm_id, task_id, Priority_t(GetTaskPriority(task_id)));
        task_assignments[task_id] = vm_id;
        SimOutput("Add_TaskToMachine(): Added " + to_string(task_id) + " on vm: " + to_string(vm_id) + " to on machine " + to_string(machine_id), 1);
    } else if (machine_states[machine_id].state == OFF) {
    // If the machine is not ON
        Machine_SetState(machine_id, S0);
        machine_states[machine_id].state = TURNING_ON;
        pending_attachments[machine_id].push_back(task_id);
        SimOutput("Add_TaskToMachine(): Turning on machine " + to_string(machine_id) + " for task " + to_string(task_id), 1);
    } else {
        ThrowException("Tried adding task to machine not ON or OFF");
    }
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
        machine_energy_rates[i] = {0, 0, Now()};
        machine_states[i] = {{}, ON, Now()};
        on_cpu_count[Machine_GetCPUType(MachineId_t(i))]++;
        state_count[S0]++;
        
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
    Can return either an ON or OFF machine. However, if returning an OFF machine, it DOES NOT turn it on for you.
*/
MachineId_t GetBestMachine(TaskId_t task_id) {
    TaskInfo_t t_info = GetTaskInfo(task_id);
    vector<MachineId_t> sorted_machines = machines;

    std::sort(sorted_machines.begin(), sorted_machines.end(), [](MachineId_t a, MachineId_t b) {
        return machine_energy_rates[a].energy_consumption_rate < machine_energy_rates[b].energy_consumption_rate;
    });

    // Iterate through sorted machines to find a suitable one
    for (const auto& machine_id : sorted_machines) {
        MachineInfo_t m_info = Machine_GetInfo(machine_id);

        // Check if the machine is ON and is compatible
        if (machine_states[machine_id].state != ON || t_info.required_cpu != m_info.cpu) {
            continue;
        }

        // place task into machine
        return machine_id;
    }
    
    // Find an off machine
    for (const auto& [machine_id, m_state] : machine_states) {
        if (m_state.state == OFF) {
            return machine_id;
        }
    }

    // Should never get this?
    return -1;
}

/*
    Sets the machine P-state by determining load on it (whether that encompasses number of cores filled, memory, number of total tasks, etc.)
*/
void SetMachinePState(MachineId_t machine_id) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    if (machine_info.s_state != S0) return;

    // calculate load factors
    double utilization = MachineUtilization(machine_id);

    if (utilization <= 1.0) {
        Machine_SetCorePerformance(machine_id, -1, P3); // Lowest performance state
    } else if (utilization <= 10.0) {
        Machine_SetCorePerformance(machine_id, -1, P2); // Low performance state
    } else if (utilization <= 20.0) {
        Machine_SetCorePerformance(machine_id, -1, P1); // Medium performance state
    } else {
        Machine_SetCorePerformance(machine_id, -1, P0); // Highest performance state
    }
    SimOutput("SetMachinePState(): Machine " + to_string(machine_id) + " set to P-state " + to_string(Machine_GetInfo(machine_id).p_state), 1);
}

/*
- For NewTask, 
    1. sort the machines in ascending energy consumption rates
    2. check if machine has enough resources/compatible
*/
void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    TaskInfo_t t_info = GetTaskInfo(task_id);
    // sort machines by energy consumption rate

    MachineId_t best_machine = GetBestMachine(task_id);
    if (best_machine != -1) {
        Add_TaskToMachine(best_machine, task_id);
        return;
    }

    // OOPS
    SimOutput("NewTask(): No suitable machine found for task " + to_string(task_id), 1);
    Debug();
    ThrowException("fuck");
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
    Runs every 60 ms or 60,000 microseconds
*/
void Scheduler::PeriodicCheck(Time_t now) {
    if (PROGRESS_BAR) {
        DisplayProgressBar();
    }
    if (MACHINE_STATE) {
        DisplayMachineStates();
    }
    if (ENERGY_BAR) {
        DebugMachineEnergy();
    }
    
    // Calculate energy rates of all machines
    for (auto& [machine_id, e_state] : machine_energy_rates) {
        uint64_t cur_consumption = Machine_GetEnergy(machine_id);
        e_state.energy_consumption_rate = Machine_GetInfo(machine_id).active_tasks/10 * (cur_consumption - e_state.prev_energy_consumption)/((now - e_state.prev_time));
        e_state.prev_energy_consumption = cur_consumption;
        e_state.prev_time = now;
    }  

    // Loop over all machines and set their P-state based on utilization
    for (const auto& machine_id : machines) {
        SetMachinePState(machine_id);
    }

    // Check for under and over utilized machines
    // LoadBalance();
    
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
    
    if (VM_GetInfo(vm_id).active_tasks.size() != 0) {
        ThrowException("Somehow there are active tasks on a VM?????");
    }

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
    // assert(machine_states[orig_m_id].vms.size() == Machine_GetInfo(orig_m_id).active_vms); 
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