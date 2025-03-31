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

machine class:
{
        Number of machines: 20
        CPU type: X86
        Number of cores: 4
        Memory: 16384
        S-States: [40, 20, 16, 12, 10, 4, 0]
        P-States: [4, 2, 2, 1]
        C-States: [4, 1, 1, 0]
        MIPS: [800, 600, 400, 200]
        GPUs: no
}

task class:
{
        Start time: 0
        End time: 2000000
        Inter arrival: 100000
        Expected runtime: 50000
        Memory: 1024
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA2
        CPU type: X86
        Task type: WEB
        Seed: 520040
}

task class:
{
        Start time: 500000
        End time: 600000
        Inter arrival: 2000
        Expected runtime: 30000
        Memory: 1024
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: WEB
        Seed: 520041
}

task class:
{
        Start time: 1500000
        End time: 1600000
        Inter arrival: 2000
        Expected runtime: 30000
        Memory: 1024
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: WEB
        Seed: 520042
}