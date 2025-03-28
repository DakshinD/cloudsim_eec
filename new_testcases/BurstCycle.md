machine class:
{
        Number of machines: 10
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
        Number of machines: 10
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
        End time: 300000
        Inter arrival: 30000
        Expected runtime: 100000
        Memory: 1024
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: WEB
        Seed: 520001
}

task class:
{
        Start time: 300000
        End time: 500000
        Inter arrival: 1000
        Expected runtime: 50000
        Memory: 1024
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: WEB
        Seed: 520002
}

task class:
{
        Start time: 500000
        End time: 800000
        Inter arrival: 30000
        Expected runtime: 100000
        Memory: 1024
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: WEB
        Seed: 520003
}

task class:
{
        Start time: 800000
        End time: 1000000
        Inter arrival: 1000
        Expected runtime: 50000
        Memory: 1024
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: WEB
        Seed: 520004
}

task class:
{
        Start time: 1000000
        End time: 1500000
        Inter arrival: 50000
        Expected runtime: 100000
        Memory: 1024
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: WEB
        Seed: 520005
}