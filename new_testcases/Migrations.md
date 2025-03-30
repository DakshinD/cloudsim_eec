machine class:
{
        Number of machines: 20
        CPU type: X86
        Number of cores: 8
        Memory: 32768
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [3000, 2400, 2000, 1500]
        GPUs: no
}

task class:
{
        Start time: 0
        End time: 1000000
        Inter arrival: 10000
        Expected runtime: 100000
        Memory: 1024
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: WEB
        Seed: 520050
}

task class:
{
        Start time: 200000
        End time: 300000
        Inter arrival: 1000
        Expected runtime: 50000
        Memory: 1024
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: WEB
        Seed: 520051
}

task class:
{
        Start time: 600000
        End time: 700000
        Inter arrival: 1000
        Expected runtime: 50000
        Memory: 1024
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: WEB
        Seed: 520052
}

task class:
{
        Start time: 400000
        End time: 500000
        Inter arrival: 2000
        Expected runtime: 80000
        Memory: 2048
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: WEB
        Seed: 520053
}