machine class:
{
        Number of machines: 16
        CPU type: X86
        Number of cores: 8
        Memory: 16384
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [3000, 2400, 2000, 1500]
        GPUs: yes
}

machine class:
{
        Number of machines: 16
        CPU type: X86
        Number of cores: 8
        Memory: 16384
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [3000, 2400, 2000, 1500]
        GPUs: no
}

machine class:
{
        Number of machines: 24
        CPU type: ARM
        Number of cores: 16
        Memory: 16384
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [1000, 800, 600, 400]
        GPUs: yes
}

machine class:
{
        Number of machines: 4
        CPU type: POWER
        Number of cores: 32
        Memory: 131072
        S-States: [120, 60, 30, 15, 8, 4, 0]
        P-States: [8, 4, 2, 1]
        C-States: [8, 4, 2, 1]
        MIPS: [1500, 1200, 1000, 800]
        GPUs: no
}
   
machine class:
{
        Number of machines: 10
        CPU type: RISCV
        Number of cores: 12
        Memory: 32768
        S-States: [100, 80, 60, 30, 10, 0]
        P-States: [10, 7, 5, 3]
        C-States: [10, 4, 2, 0]
        MIPS: [1200, 1000, 800, 600]
        GPUs: yes
}

task class:
{
        Start time: 10000
        End time : 500000
        Inter arrival: 500
        Expected runtime: 100000
        Memory: 16
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: WEB
        Seed: 12345
}

task class:
{
        Start time: 3000000
        End time : 7000000
        Inter arrival: 2000
        Expected runtime: 500000
        Memory: 8
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA2
        CPU type: ARM
        Task type: STREAM
        Seed: 67890
}

task class:
{
        Start time: 50000
        End time : 20000000
        Inter arrival: 10000
        Expected runtime: 15000000
        Memory: 32
        VM type: WIN
        GPU enabled: no
        SLA type: SLA3
        CPU type: POWER
        Task type: STREAM
        Seed: 54321
}

task class:
{
        Start time: 60000
        End time : 120000
        Inter arrival: 500
        Expected runtime: 50000
        Memory: 4
        VM type: LINUX_RT
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: WEB
        Seed: 98765
}

task class:
{
        Start time: 25000000
        End time : 300000000
        Inter arrival: 5000
        Expected runtime: 10000000
        Memory: 64
        VM type: LINUX
        GPU enabled: yes
        SLA type: SLA1
        CPU type: X86
        Task type: AI
        Seed: 192837
}
