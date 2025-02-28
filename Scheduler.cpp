//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"
#include <map>

using std::map;

static bool migrating = false;
static unsigned active_machines = 16;

// Data structures for scheduling state
map<TaskId_t, VMId_t> task_to_vm;
map<VMId_t, MachineId_t> vm_to_machine; 


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

    // TBD: update vm_to_machine state here?
    for(unsigned i = 0; i < active_machines; i++)
        vms.push_back(VM_Create(LINUX, X86));
    for(unsigned i = 0; i < active_machines; i++) {
        machines.push_back(MachineId_t(i));
    }    
    for(unsigned i = 0; i < active_machines; i++) {
        VM_Attach(vms[i], machines[i]);
    }

    bool dynamic = false;
    if(dynamic)
        for(unsigned i = 0; i<4 ; i++)
            for(unsigned j = 0; j < 8; j++)
                Machine_SetCorePerformance(MachineId_t(0), j, P3);
    // Turn off the ARM machines
    for(unsigned i = 24; i < Machine_GetTotal(); i++)
        Machine_SetState(MachineId_t(i), S5);

    SimOutput("Scheduler::Init(): VM ids are " + to_string(vms[0]) + " ahd " + to_string(vms[1]), 3);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    // Get the task parameters
    //  IsGPUCapable(task_id);
    //  GetMemory(task_id);
    //  RequiredVMType(task_id);
    //  RequiredSLA(task_id);
    //  RequiredCPUType(task_id);
    // Decide to attach the task to an existing VM, 
    //      vm.AddTask(taskid, Priority_T priority); or
    // Create a new VM, attach the VM to a machine
    //      VM vm(type of the VM)
    //      vm.Attach(machine_id);
    //      vm.AddTask(taskid, Priority_t priority) or
    // Turn on a machine, create a new VM, attach it to the VM, then add the task
    //
    // Turn on a machine, migrate an existing VM from a loaded machine....
    //
    // Other possibilities as desired
    Priority_t priority = (task_id == 0 || task_id == 64)? HIGH_PRIORITY : MID_PRIORITY;
    if(migrating) {
        VM_AddTask(vms[0], task_id, priority);
    }
    else {
        VM_AddTask(vms[task_id % active_machines], task_id, priority);
    }// Skeleton code, you need to change it according to your algorithm

    // GUIDELINES:
    // We are currently trying to add a new task
    /*
    ADDING A TASK
    ---------------
    1. We add this task to an existing VM on a running machine
        a. Our VM could either already have tasks on it (priority conflicts), 
            or for some reason have no tasks (it should've been shutdown)
        b. By adding this task, we could exceed memory limits and have to 
            migrate this VM to a different machine (either a on one, or wake
            a new machine)
    2. We add this task by starting a new VM on a running machine
        a. Make sure that # of VMs < # of Cores for optimal performance (priority)
        b. Can we satisfy memory requirements
        c. We should choose a machine that was already on, we don't want to 
            start a new machine for a single VM
                (i). If we do start a new machine for this VM, (priority reasons?)
                    We want to load balance the tasks from other machines to this
                    new machine depending on number of cores? (Trade offs)
    3. We add this task by migrating a VM to an already running machine
        a. Why would we do this? Load balancing, taking advantage of free cores,
        memory requirements, maybe this VM is running SLA3 and we have a SLA0 task
        but if we added this task memory would exceed, so using this VM on a different machine
        is perfect for priority. But is this more optimal then just starting another VM?
    4. We add this task by migrating a VM to a sleeping machine
        a. Load balancing scenarios, we can take advantage of VMs with low priority to
        migrate, but all other machines are already loaded
    5. We add this task by starting a new VM on a sleeping machine
        a. Worst case scenario, starting a machine with only one VM requires us
            to now load balance all the other machines with this machine for 
            optimal usage

    ON TASK COMPLETION
    ---------------
    1. If it was the last task on the VM
        a. If it was the last VM on the machine, shut it down?
        b. Load balance between VMs on the same machine or different machines?
            (i) If one VM has > 1 task and this VM is empty now, balance
            (ii) Is it more energy efficient to have tasks on the other VM instead of 
                keeping this one open if we meet SLA cutoffs?
    2. If it wasn't last task on the VM
        (a) Maybe we have space on this VM now to add SLA3, low priority tasks
            to here and free up higher priority tasks to run by themselves
    
    BASIC POLICIES:
    ---------------
        1. Is it better to have # VMs == # Cores in all scenarios,
            or is it better for energy if # VMs > # Cores with less machines
            while still passing SLA?
        2. When # VMs < # Cores, we should just add the VM to that machine,
            unless there is a problem with priority, and we want to group
            the high priority tasks on the same machine but sparsely
        3. High priority tasks should be by themselves on the VM
        4. We are fine with putting low priority tasks on the same VM,
            could also save energy
        5. We want to prevent # VMs > # Cores, when can we allow this?
            For low priority tasks? Or will this save us energy instead
            of running another server at risk of violating SLA?
        6. When can we value energy over latency?

    */
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
    migrating = false;
}

void SchedulerCheck(Time_t time) {
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
    static unsigned counts = 0;
    counts++;
    if(counts == 10) {
        migrating = true;
        VM_Migrate(1, 9);
    }
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

