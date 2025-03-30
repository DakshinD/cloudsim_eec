machine class:
{
        Number of machines: 5
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
        Number of machines: 5
        CPU type: X86
        Number of cores: 8
        Memory: 65536
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [3000, 2400, 2000, 1500]
        GPUs: no
}

machine class:
{
        Number of machines: 5
        CPU type: X86
        Number of cores: 8
        Memory: 131072
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
        Expected runtime: 200000
        Memory: 8192
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: WEB
        Seed: 520020
}

task class:
{
        Start time: 200000
        End time: 1200000
        Inter arrival: 5000
        Expected runtime: 100000
        Memory: 4096
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: WEB
        Seed: 520021
}

task class:
{
        Start time: 0
        End time: 1200000
        Inter arrival: 2000
        Expected runtime: 50000
        Memory: 512
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA2
        CPU type: X86
        Task type: WEB
        Seed: 520022
}