machine class:
{
        Number of machines: 25
        CPU type: X86
        Number of cores: 8
        Memory: 65536
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [4000, 3200, 2400, 1600]
        GPUs: no
}

task class:
{
        Start time: 0
        End time: 1000000
        Inter arrival: 2000
        Expected runtime: 100000
        Memory: 2048
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: WEB
        Seed: 520030
}

task class:
{
        Start time: 0
        End time: 1000000
        Inter arrival: 4000
        Expected runtime: 150000
        Memory: 2048
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: WEB
        Seed: 520031
}

task class:
{
        Start time: 0
        End time: 1000000
        Inter arrival: 12000
        Expected runtime: 200000
        Memory: 2048
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA2
        CPU type: X86
        Task type: WEB
        Seed: 520032
}