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

using std::map;
using std::set;

enum MachinePowerState {
    ON = 0,
    TURNING_ON = 1, 
    TURNING_OFF = 2,
    OFF = 3,
    STANDBY = 4,
};

struct MachineState { 
    set<VMId_t> vms;
    MachinePowerState state;
};

const MachineState_t SLEEP_STATE = S5; // State we initially shut down empty PM to
double MIN_MACHINE_PERCENT_IN_STATE = 0.5;
const bool PROGRESS_BAR = true;
const bool MACHINE_STATE = false;
const bool TEST = false;

// Progress Bar
unsigned total_tasks = 0;
unsigned completed_tasks = 0;

// Data Structures for state tracking
unsigned total_machines = -1;
unsigned total_on_machines = -1;

vector<MachineId_t> machines;
map<TaskId_t, VMId_t> task_assignments;
vector<VMId_t> vms;
map<MachineId_t, MachineState> machine_states;
map<MachineId_t, vector<TaskId_t>> pending_attachments; // We want to add VMs to a machine that is transitioning to ON but isnt ON yet.
map<VMId_t, MachineId_t> ongoing_migrations;

// Data Structures for E-eco implementation
const double num_on_machines_init = .6;
const double num_standby_machines_init = .3;
const double num_off_machines_init = .1;
map<CPUType_t, unsigned> tasks_per_cpu_type;
map<CPUType_t, double> avg_memory_per_task_of_cpu_type;
const MachineState_t STANDBY_STATE = S0i1;

// Reporting Data Structures
int total_sla[NUM_SLAS] = {0};
int sla_violations[NUM_SLAS] = {0};

// Func headers
void MigrateHelper(VMId_t vm_id, MachineId_t start_m, MachineId_t end_m);
void Debug();
void DebugVM(VMId_t vm_id);
double MachineUtilization(MachineId_t machine_id);
// ----------------------------------------------------------------------------------------------------------------

/* Debugging Methods */

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

void PrintMachineStateDistribution() {
     // Group machines by CPU type and count states
    map<CPUType_t, vector<unsigned>> cpu_state_counts;  // [ON, STANDBY, OFF] counts per CPU

    for(const auto& [machine_id, m_state] : machine_states) {
        CPUType_t cpu_type = Machine_GetCPUType(machine_id);
        
        // Initialize counts vector if this is first machine of this CPU type
        if(cpu_state_counts.find(cpu_type) == cpu_state_counts.end()) {
            cpu_state_counts[cpu_type] = {0, 0, 0};  // [ON, STANDBY, OFF]
        }
        
        // Count based on state
        if(m_state.state == ON) {
            cpu_state_counts[cpu_type][0]++;
        } else if(m_state.state == STANDBY) {
            cpu_state_counts[cpu_type][1]++;
        } else if(m_state.state == OFF) {
            cpu_state_counts[cpu_type][2]++;
        }
    }

    // Create summary string
    string summary = "\nMachine State Distribution by CPU Type:\n";
    summary += "----------------------------------------\n";
    
    for(const auto& [cpu_type, counts] : cpu_state_counts) {
        unsigned total = counts[0] + counts[1] + counts[2];
        summary += "CPU Type " + to_string(cpu_type) + ":\n";
        summary += "  ON:      " + to_string(counts[0]) + " (" + 
                  to_string((counts[0] * 100.0) / total) + "%)\n";
        summary += "  STANDBY: " + to_string(counts[1]) + " (" + 
                  to_string((counts[1] * 100.0) / total) + "%)\n";
        summary += "  OFF:     " + to_string(counts[2]) + " (" + 
                  to_string((counts[2] * 100.0) / total) + "%)\n";
        summary += "  Total:   " + to_string(total) + "\n";
        summary += "----------------------------------------\n";
    }

    SimOutput(summary, 1);
}

/* Migration methods */

bool Machine_IsMigrationTarget(MachineId_t machine_id) {
    // Check if this machine is a target for migration
    for (const auto& [vm_id, m_id] : ongoing_migrations) {
        if (m_id == machine_id) return true;
    }
    return false;
}

void MigrateHelper(VMId_t vm_id, MachineId_t start_m, MachineId_t end_m) {
    // SimOutput("Start migration " + to_string(vm_id) + " from " + to_string(start_m) + " to " + to_string(end_m), 1);
    VM_Migrate(vm_id, end_m);

    // Update the data structures
    machine_states[start_m].vms.erase(vm_id);
    ongoing_migrations[vm_id] = end_m;
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
    // SimOutput("MigrationComplete(): Migration of VM " + to_string(vm_id) + " completed at time " + to_string(time), 1);
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    machine_states[vm_info.machine_id].vms.insert(vm_id);
    ongoing_migrations.erase(vm_id);

    // If there are no tasks on this VM, we can shut it down
    if (VM_GetInfo(vm_id).active_tasks.size() == 0) {
        // We can shutdown this VM
        // SimOutput("MigrationComplete(): VM " + to_string(vm_id) + " is now empty and is being shut down", 1);
        VM_Shutdown(vm_id);
        // Remove the VM from the machine
        MachineId_t m_id = vm_info.machine_id;
        machine_states[m_id].vms.erase(vm_id);
    }
}


void Scheduler::Init() {
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);

    total_machines = Machine_GetTotal();
    total_tasks = GetNumTasks();

    // Group machines by CPU type first
    map<CPUType_t, vector<MachineId_t>> machines_by_cpu;
    for(unsigned i = 0; i < total_machines; i++) {
        machines.push_back(MachineId_t(i));
        CPUType_t cpu_type = Machine_GetCPUType(i);
        machines_by_cpu[cpu_type].push_back(i);
    }

    // For each CPU type, distribute machines according to e-eco ratios
    for(auto& [cpu_type, cpu_machines] : machines_by_cpu) {
        unsigned cpu_total = cpu_machines.size(); 
        
        // Calculate number of machines for each state using e-eco ratios
        unsigned num_on = cpu_total * num_on_machines_init;        
        unsigned num_standby = cpu_total * num_standby_machines_init; 
        unsigned num_off = cpu_total * num_off_machines_init;      

        // Distribute machines across states
        unsigned machine_index = 0;
        
        // Set ON machines
        for(unsigned i = 0; i < num_on; i++) {
            MachineId_t machine_id = cpu_machines[machine_index++];
            machine_states[machine_id] = {{}, ON};
            Machine_SetState(machine_id, S0);
            total_on_machines++;
        }

        // Set STANDBY machines
        for(unsigned i = 0; i < num_standby; i++) {
            MachineId_t machine_id = cpu_machines[machine_index++];
            machine_states[machine_id] = {{}, STANDBY};
            Machine_SetState(machine_id, STANDBY_STATE);
        }
        
        // Set remaining machines to OFF
        while(machine_index < cpu_machines.size()) {
            MachineId_t machine_id = cpu_machines[machine_index++];
            machine_states[machine_id] = {{}, OFF};
            Machine_SetState(machine_id, S5);
        }
    }  

    SimOutput("Initial machine state distribution:", 1);
    PrintMachineStateDistribution();
}

/* Adding a task methods */


/*
    Add task to a machine, no matter the state it is in
*/
void Add_TaskToMachine(MachineId_t machine_id, TaskId_t task_id) {
    // If the machine is ON
    if (machine_states[machine_id].state == ON) {
        VMId_t vm_id = VM_Create(RequiredVMType(task_id), RequiredCPUType(task_id));

        VM_Attach(vm_id, machine_id);
        machine_states[machine_id].vms.insert(vm_id);

        VM_AddTask(vm_id, task_id, Priority_t(GetTaskPriority(task_id)));
        task_assignments[task_id] = vm_id;
        // SimOutput("Add_TaskToMachine(): Added " + to_string(task_id) + " on vm: " + to_string(vm_id) + " to on machine " + to_string(machine_id), 1);
    } else {
    // If the machine is not ON
        Machine_SetState(machine_id, S0);
        machine_states[machine_id].state = TURNING_ON;
        pending_attachments[machine_id].push_back(task_id);
        // SimOutput("Add_TaskToMachine(): Turning on machine " + to_string(machine_id) + " for pending task " + to_string(task_id), 1);
    }
    return;
}

/* Used to choose the first machine we can to add to*/
struct MachineScore {
    MachineId_t id;
    int category;  
    // 1: Both CPU & Memory
    // 2: CPU only 
    // 3: Memory only 
    // 4: Off machine
    // 5: Any machine
    
    bool operator<(const MachineScore& other) const {
        return category < other.category;
    }
};

void PrintCPUTypeDistribution(CPUType_t cpu_type) {
    // Count machines in each state for this CPU type
    unsigned on_count = 0;
    unsigned standby_count = 0;
    unsigned off_count = 0;
    unsigned total = 0;

    for(const auto& [machine_id, m_state] : machine_states) {
        if(Machine_GetCPUType(machine_id) == cpu_type) {
            total++;
            if(m_state.state == ON) {
                on_count++;
            } else if(m_state.state == STANDBY) {
                standby_count++;
            } else if(m_state.state == OFF) {
                off_count++;
            }
        }
    }

    string summary = "CPU Type " + to_string(cpu_type) + " Distribution: ";
    summary += "ON: " + to_string(on_count) + " (" + 
              to_string((on_count * 100.0) / total) + "%), ";
    summary += "STANDBY: " + to_string(standby_count) + " (" + 
              to_string((standby_count * 100.0) / total) + "%), ";
    summary += "OFF: " + to_string(off_count) + " (" + 
              to_string((off_count * 100.0) / total) + "%)";

    SimOutput(summary, 1);
}

/*
    2 ways to be greedy (mimic utilization):
        1. memory
        2. cpu cores/# of vms
*/
void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    SimOutput("NewTask(): New task at time: " + to_string(now), 1);

    CPUType_t cpu_type = RequiredCPUType(task_id);
    unsigned task_memory = GetTaskMemory(task_id);

    // calculate the average memory per task of this cpu type
    tasks_per_cpu_type[cpu_type]++;
    avg_memory_per_task_of_cpu_type[cpu_type] = (avg_memory_per_task_of_cpu_type[cpu_type] * (tasks_per_cpu_type[cpu_type] - 1) + task_memory) / tasks_per_cpu_type[cpu_type];

    unsigned num_cores_available = 0;
    unsigned memory_available = 0;

    for (const auto& [machine_id, m_state] : machine_states) {
        if ((m_state.state == ON || m_state.state == TURNING_ON) && cpu_type == Machine_GetCPUType(machine_id)) {
            MachineInfo_t machine_info = Machine_GetInfo(machine_id);
            num_cores_available += (machine_info.num_cpus - machine_info.active_vms);
            memory_available += (machine_info.memory_size - machine_info.memory_used);
        }
    }

    if (num_cores_available <= 6 || memory_available <= (3 * avg_memory_per_task_of_cpu_type[cpu_type])) {
        // We need to turn on a machine from standby
        SimOutput("NewTask(): Need to turn on a machine from standby", 1);
        for (const auto& [machine_id, m_state] : machine_states) {
            if (m_state.state == STANDBY && cpu_type == Machine_GetCPUType(machine_id)) {
                Machine_SetState(machine_id, S0);
                machine_states[machine_id].state = TURNING_ON;
                break;
            }
        }

        // Check if we need to turn a machine from off to standby
        double standby_ratio = 0;
        unsigned total_machines_of_cpu_type = 0;
        for (const auto& [machine_id, m_state] : machine_states) {
            if (cpu_type == Machine_GetCPUType(machine_id)) {
                total_machines_of_cpu_type++;
                if (m_state.state == STANDBY) standby_ratio++;
            }
        }

        standby_ratio = standby_ratio / total_machines_of_cpu_type;
        if (standby_ratio < (.1 + num_standby_machines_init)) {
            // We need to turn on a machine from off if there are remaining off machines
            for (const auto& [machine_id, m_state] : machine_states) {
                if (m_state.state == OFF && cpu_type == Machine_GetCPUType(machine_id)) {
                    Machine_SetState(machine_id, STANDBY_STATE);
                    machine_states[machine_id].state = TURNING_OFF;
                    break;
                }
            }
        }
    }    


    // Get the task parameters
    // Get the task parameters
    TaskInfo_t task = GetTaskInfo (task_id);

    vector<MachineScore> scored_machines;
    
    // score all machines
    for (const auto& [machine_id, m_state] : machine_states) {
        if (RequiredCPUType(task_id) != Machine_GetCPUType(machine_id)) {
            continue;
        }
        
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        bool has_cpu = machine_info.active_vms < machine_info.num_cpus;
        bool has_memory = (machine_info.memory_size - machine_info.memory_used) >= GetTaskMemory(task_id);
        
        MachineScore score{machine_id, 6};  // Default: category 5 (any machine)
        
        if (m_state.state == ON) {
            if (has_cpu && has_memory) score.category = 1;
            else if (has_cpu) score.category = 2;
            else if (has_memory) score.category = 3;
        } else if (m_state.state == STANDBY) {
            score.category = 4;
        } else if (m_state.state == OFF) {
            score.category = 5;
        }
        
        scored_machines.push_back(score);
    }
    
    // sort by category 
    std::sort(scored_machines.begin(), scored_machines.end(), [](const MachineScore& a, const MachineScore& b) {
        if (a.category == b.category) {
            double util_a = MachineUtilization(a.id);
            double util_b = MachineUtilization(b.id);
            return util_a < util_b; // Tie-break by lower utilization
        }
        return a.category < b.category;
    });
    
    // take best available machine
    if (!scored_machines.empty()) {
        Add_TaskToMachine(scored_machines[0].id, task_id);
        return;
    }
    
    ThrowException("Scheduler::NewTask(): No compatible machines found for task " + to_string(task_id));
}

void Scheduler::PeriodicCheck(Time_t now) {
    if (PROGRESS_BAR) {
        DisplayProgressBar();
    }
    if (MACHINE_STATE) {
        DisplayMachineStates();
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
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

/*
    We need a method to calculate utilization (cores, tasks, VMs, memory)
    This ranges from 0 and theoretically has NO upper bound. It is used for relative comparisons.
*/
double MachineUtilization (MachineId_t machine_id) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);

    const double W_CORE = 3.0;
    const double W_MEM = 1.0;

    // calculate utilization based on cores/VMs, and memory
    double core_utilization = (double)machine_info.active_vms / machine_info.num_cpus;
    double memory_utilization = (double)machine_info.memory_used / machine_info.memory_size;
    
    // combine the utilization metrics into a single score
    double utilization_score = ((W_CORE * core_utilization) + (W_MEM * memory_utilization)) / 2.0;

    return utilization_score;
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    VMId_t vm_id = task_assignments[task_id];
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    // SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now) + " on vm " + to_string(vm_id), 1);
    if (VM_GetInfo(vm_id).active_tasks.size() != 0) {
        ThrowException("Somehow there are active tasks on a VM?????");
    }

    // update progress + DS
    completed_tasks++;
    total_sla[RequiredSLA(task_id)]++;
    task_assignments.erase(task_id);

    // delete the VM - only do this because there was only one task
    MachineId_t orig_m_id = vm_info.machine_id;
    machine_states[orig_m_id].vms.erase(vm_id);
    if (!ongoing_migrations.count(vm_id)) {
        VM_Shutdown(vm_id);
        // SimOutput("Shutdown(): vm " + to_string(vm_id), 1);
    }
    
    // sort machines by utilization in ascending order
    const double UTIL_THRESHOLD = 0.1; 
    vector<pair<MachineId_t, double>> sorted_machines;
    
    for (const auto& [machine_id, m_state] : machine_states) {
        if (m_state.state == ON) 
            sorted_machines.emplace_back(machine_id, MachineUtilization(machine_id));
    }
    
    sort(sorted_machines.begin(), sorted_machines.end(),
         [](const auto& a, const auto& b) { return a.second < b.second; });

    // process underutilized machines
    for (size_t i = 0; i < sorted_machines.size(); i++) {
        MachineId_t source_id = sorted_machines[i].first;
        double source_util = sorted_machines[i].second;
        
        // skip machines above threshold
        if (source_util > UTIL_THRESHOLD) break;
        
        // Try to migrate all VMs from this machine
        bool all_vms_migrated = true;
        
        set<VMId_t> vms_to_migrate = machine_states[source_id].vms;
        for (auto vm_id : vms_to_migrate) {
            bool vm_migrated = false;
            TaskId_t task_id = VM_GetInfo(vm_id).active_tasks[0];  // one task per VM
            
            // Look for target machines with higher utilization
            for (size_t j = i + 1; j < sorted_machines.size(); j++) {
                MachineId_t target_id = sorted_machines[j].first;
                MachineInfo_t target_info = Machine_GetInfo(target_id);
                
                // Check if migration is possible
                if (RequiredCPUType(task_id) == Machine_GetCPUType(target_id)) {
                    MigrateHelper(vm_id, source_id, target_id);
                    vm_migrated = true;
                    break;
                }
            }
            if (!vm_migrated) {
                all_vms_migrated = false;
                break;  // If we can't migrate one VM, stop trying others
            }

        }
        // If machine is empty after migrations, shut it down
        MachineInfo_t m_info = Machine_GetInfo(source_id);
        if (all_vms_migrated && m_info.active_vms == 0 && 
            total_on_machines > int(MIN_MACHINE_PERCENT_IN_STATE * total_machines) && !Machine_IsMigrationTarget(source_id)) {
            
            Machine_SetState(source_id, SLEEP_STATE);
            machine_states[source_id].state = TURNING_OFF;
            total_on_machines--;
            // SimOutput("TaskComplete(): Machine " + to_string(source_id) + 
            //          " is empty after migrations and is being turned off", 1);
        }
    }

    // Check if we need to turn a machine from standby to off or on to standby
    MachineId_t m_id = vm_info.machine_id;
    CPUType_t cpu_type = Machine_GetCPUType(m_id);

    if (!Machine_IsMigrationTarget(m_id)) {
        // Count machines and calculate available resources for this CPU type
        unsigned total_cpu_machines = 0;
        unsigned current_on = 0;
        unsigned current_standby = 0;
        unsigned total_available_cores = 0;
        unsigned total_available_memory = 0;
        
        for (const auto& [machine_id, m_state] : machine_states) {
            if (Machine_GetCPUType(machine_id) == cpu_type) {
                total_cpu_machines++;
                MachineInfo_t m_info = Machine_GetInfo(machine_id);
                
                if (m_state.state == ON) {
                    current_on++;
                    total_available_cores += (m_info.num_cpus - m_info.active_vms);
                    total_available_memory += (m_info.memory_size - m_info.memory_used);
                } else if (m_state.state == STANDBY) {
                    current_standby++;
                }
            }
        }

        if (Machine_GetInfo(m_id).active_vms == 0) {
            // Check if we can turn this ON machine to STANDBY
            // Ensure we maintain minimum resources after turning this machine off
            if (total_available_cores >= 6 && 
                total_available_memory > (3 *avg_memory_per_task_of_cpu_type[cpu_type])) {
                
                // Turn this machine to STANDBY
                Machine_SetState(m_id, STANDBY_STATE);
                machine_states[m_id].state = TURNING_OFF;
                current_on--;
                total_on_machines--;
                current_standby++;
                
                SimOutput("TaskComplete(): Machine " + to_string(m_id) + 
                         " transitioning to STANDBY. Available cores: " + 
                         to_string(total_available_cores) + ", memory: " + 
                         to_string(total_available_memory), 1);

                // Check if we have too many STANDBY machines
                unsigned max_standby = total_cpu_machines * num_standby_machines_init;
                if (current_standby > max_standby) {
                    // Find a STANDBY machine to turn OFF
                    for (auto& [machine_id, m_state] : machine_states) {
                        if (m_state.state == STANDBY && 
                            Machine_GetCPUType(machine_id) == cpu_type &&
                            machine_id != m_id) {  // Don't turn off the machine we just transitioned
                            
                            Machine_SetState(machine_id, S5);
                            m_state.state = TURNING_OFF;
                            SimOutput("TaskComplete(): Excess STANDBY machine " + 
                                     to_string(machine_id) + " turning OFF", 1);
                            break;
                        }
                    }
                }
            }
        }
    }
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

    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    TaskInfo_t task = GetTaskInfo(task_id);
    sla_violations[task.required_sla]++;

    // Get the problematic machine
    MachineId_t problem_machine = VM_GetInfo(task_assignments[task_id]).machine_id;
    double initial_util = MachineUtilization(problem_machine);
    double target_util = initial_util / 2.0;  // Try to halve the utilization
    
    SimOutput("SLAWarning(): Got violation for task " + to_string(task_id) + 
              " at time " + to_string(time) + 
              " on machine " + to_string(problem_machine) + 
              " (util: " + to_string(initial_util) + ")", 1);
    
    if (initial_util > 0.8) {  // only migrate if utilization is significant
        vector<pair<MachineId_t, double>> sorted_machines;
        // get all ON machines except the problem machine
        for (const auto& [machine_id, m_state] : machine_states) {
            if (m_state.state == ON && machine_id != problem_machine) {
                sorted_machines.emplace_back(machine_id, MachineUtilization(machine_id));
            }
        }
        
        // sort by utilization ascending
        sort(sorted_machines.begin(), sorted_machines.end(),
             [](const auto& a, const auto& b) { return a.second < b.second; });

        // Try to migrate VMs until target utilization reached
        set<VMId_t> problem_vms = machine_states[problem_machine].vms;
        
        for (auto vm_id : problem_vms) {

            if (MachineUtilization(problem_machine) <= target_util) {
                break;  // stop if we've reached target utilization
            }

            // try each potential target machine
            for (const auto& [target_id, target_util] : sorted_machines) {
                MachineInfo_t target_info = Machine_GetInfo(target_id);
                
                // check migration compatibility
                if (Machine_GetInfo(problem_machine).cpu == Machine_GetCPUType(target_id)) {
                    MigrateHelper(vm_id, problem_machine, target_id);
                    SimOutput("SLAWarning(): Migrating VM " + to_string(vm_id) + 
                            " to reduce load on machine " + to_string(problem_machine), 1);
                    break;
                }
            }
        }
        
        SimOutput("SLAWarning(): Machine " + to_string(problem_machine) + " utilization after migrations: " + to_string(MachineUtilization(problem_machine)), 1);
    }

    if (TEST) {
        Debug();
        ThrowException("SLA Violation for task " + to_string(task_id) + " on machine " + to_string(problem_machine) + " at time " + to_string(time));
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
    } else if (m_info.s_state == STANDBY_STATE) {
        machine_states[machine_id].state = STANDBY;

        if (pending_attachments[machine_id].size() > 0) {
            Machine_SetState(machine_id, S0);
            machine_states[machine_id].state = TURNING_ON;
        }
    } else {
        // If we turned it off
        machine_states[machine_id].state = OFF; 
        // But we added pending tasks while turning it off, then we need to turn it back on OOPS! Really slow
        // Had to allow adding pending tasks to transitiong machines so it could work. Probably not OK.
        if (pending_attachments[machine_id].size() > 0) {
            Machine_SetState(machine_id, S0);
            machine_states[machine_id].state = TURNING_ON;
        }
    }

    PrintCPUTypeDistribution(Machine_GetCPUType(machine_id));

}