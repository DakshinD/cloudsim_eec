machine class:
{
        Number of machines: 15
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
        CPU type: ARM
        Number of cores: 4
        Memory: 16384
        S-States: [40, 20, 16, 12, 10, 4, 0]
        P-States: [4, 2, 2, 1]
        C-States: [4, 1, 1, 0]
        MIPS: [1200, 1000, 800, 600]
        GPUs: no
}

machine class:
{
        Number of machines: 5
        CPU type: X86
        Number of cores: 8
        Memory: 65536
        S-States: [150, 120, 100, 80, 40, 10, 0]
        P-States: [16, 12, 8, 4]
        C-States: [16, 4, 1, 0]
        MIPS: [4000, 3200, 2400, 1600]
        GPUs: yes
}

task class:
{
        Start time: 0
        End time: 1000000
        Inter arrival: 5000
        Expected runtime: 200000
        Memory: 2048
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: CRYPTO
        Seed: 520010
}

task class:
{
        Start time: 200000
        End time: 1200000
        Inter arrival: 8000
        Expected runtime: 150000
        Memory: 1024
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: ARM
        Task type: WEB
        Seed: 520011
}

task class:
{
        Start time: 400000
        End time: 1400000
        Inter arrival: 15000
        Expected runtime: 300000
        Memory: 4096
        VM type: LINUX
        GPU enabled: yes
        SLA type: SLA0
        CPU type: X86
        Task type: AI
        Seed: 520012
}