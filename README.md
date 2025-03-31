# CloudSim EEC
By Dakshin Devanand & Krish Shah

This repository contains implementations of various algorithms for cloud simulation, including `greedy`, `e-eco`, `dvfs`, and `experimental`. Below are instructions on how to run individual files and the test suite.

---

## Branches

This repository contains the following branches, each corresponding to a specific algorithm or experimental setup:

- **`greedy`**: Contains the implementation of the Greedy algorithm.
- **`e-eco`**: Contains the implementation of the E-ECO algorithm (this is our algo from literature, referenced slides and [paper](https://www.researchgate.net/publication/310451800_E-eco_Performance-Aware_Energy-Efficient_Cloud_Data_Center_Orchestration)).
- **`dvfs`**: Contains an implementation that combines dvfs P-state manipulation with placement/migration policies from the MBFD algorithm ([research paper](https://www.sciencedirect.com/science/article/pii/S0167739X11000689)).
- **`experimental`**: Contains experimental implementations for efficiency (**best** algo of ours).

To switch to a specific branch, use:
```bash
git checkout <branch-name>
```

Replace `<branch-name>` with one of the branch names listed above.

Example:
```bash
git checkout greedy
```

---


## Running Individual Testcases

Each algorithm is implemented in its respective Scheduler.cpp file. To run an individual algorithm, checkout to the specific branch: `greedy`, `e-eco`, `dvfs`, and `experimental`.

```bash
./simulator <filepath>.md
```

---

## Running the Automatic Test Suite

> **Note**: We have already ran the test suite and left the results in `test_results` dir, but you can follow the below instructions to re-run it yourself.


To run the test suite, use the provided Python test file. Execute the following command:

```bash
python3 run_tests.py
```

You will then be prompted to choose what testcase set you want to run:
1. Standard test cases provided by TAs (located in the `testcases` directory).
2. Custom test cases created by us (located in the `new_testcases` directory).
3. Combined test cases (both standard and custom).

Wait a bit and in the `test_results` dir, you'll see 2 files called `test_report_<standard/new/combined>_<timestamp>`. One is a `.txt` file
for easy access and the other is a `.html` file you can download and open in browser to see results.

---

