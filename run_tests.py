import os
import subprocess
from datetime import datetime
import re

def strip_ansi(text):
    """Remove ANSI escape sequences"""
    ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    return ansi_escape.sub('', text)

def parse_sla_line(line):
    """Parse SLA line and return dict with compliance info"""
    # Strip ANSI escape sequences first
    clean_line = strip_ansi(line)
    match = re.match(r"SLA(\d+): Violations = (\d+)/(\d+) \(([\d.]+)% violations, ([\d.]+)% compliance\) \[Required: ([\d.]+)% compliance\]", clean_line)
    if match:
        sla_id = int(match.group(1))
        violations = int(match.group(2))
        total = int(match.group(3))
        compliance = float(match.group(5))
        required = float(match.group(6))
        passed = compliance >= required
        return {
            'sla_id': sla_id,
            'violations': violations,
            'total': total,
            'compliance': compliance,
            'required': required,
            'passed': passed
        }
    return None

def generate_html_report(results, filename):
    """Generate HTML report with colored results"""
    html_template = """
    <!DOCTYPE html>
    <html>
    <head>
        <title>CloudSim Test Results - {timestamp}</title>
        <style>
            body {{ 
                font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif; 
                margin: 40px auto;
                max-width: 1200px;
                padding: 0 20px;
                line-height: 1.6;
            }}
            .test-case {{ 
                margin-bottom: 30px;
                border: 1px solid #e1e4e8;
                border-radius: 6px;
                padding: 20px;
                background: #fff;
                box-shadow: 0 1px 3px rgba(0,0,0,0.1);
            }}
            .test-header {{
                border-bottom: 1px solid #e1e4e8;
                margin-bottom: 15px;
                padding-bottom: 10px;
            }}
            .pass {{ color: #28a745; }}
            .fail {{ color: #dc3545; }}
            .sla-table {{
                width: 100%;
                border-collapse: collapse;
                margin: 10px 0;
            }}
            .sla-table th, .sla-table td {{
                padding: 8px;
                text-align: left;
                border: 1px solid #e1e4e8;
            }}
            .sla-table th {{
                background: #f6f8fa;
            }}
            .metrics {{
                display: flex;
                gap: 20px;
                margin-top: 15px;
            }}
            .metric {{
                padding: 10px;
                background: #f6f8fa;
                border-radius: 4px;
            }}
            .summary {{
                margin-bottom: 30px;
                padding: 20px;
                background: #f8f9fa;
                border-radius: 6px;
            }}
        </style>
    </head>
    <body>
        <h1>CloudSim Test Results - {timestamp}</h1>
        {content}
    </body>
    </html>
    """

    test_case_template = """
        <div class="test-case">
            <div class="test-header">
                <h2>{testcase_name}</h2>
            </div>
            <table class="sla-table">
                <tr>
                    <th>SLA Level</th>
                    <th>Compliance</th>
                    <th>Required</th>
                    <th>Violations</th>
                    <th>Status</th>
                </tr>
                {sla_rows}
            </table>
            <div class="metrics">
                <div class="metric">
                    <strong>Total Energy:</strong> {energy} KW-Hour
                </div>
                <div class="metric">
                    <strong>Runtime:</strong> {runtime} seconds
                </div>
            </div>
        </div>
    """

    content = []
    all_passed = True

    # Track passed/failed counts
    total_tests = len(results)
    passed_tests = 0
    failed_tests = []

    for testcase_name, test_data in sorted(results.items()):
        sla_rows = []
        testcase_passed = True
        for sla in test_data['sla_results']:
            status_class = "pass" if sla['passed'] else "fail"
            status_text = "✓ PASS" if sla['passed'] else "✗ FAIL"
            
            sla_rows.append(f"""
                <tr class="{status_class}">
                    <td>SLA{sla['sla_id']}</td>
                    <td>{sla['compliance']:.2f}%</td>
                    <td>{sla['required']}%</td>
                    <td>{sla['violations']}/{sla['total']}</td>
                    <td>{status_text}</td>
                </tr>
            """)
            if not sla['passed']:
                testcase_passed = False
        
        if testcase_passed:
            passed_tests += 1
        else:
            failed_tests.append(testcase_name)

        content.append(test_case_template.format(
            testcase_name=testcase_name,
            sla_rows=''.join(sla_rows),
            energy=test_data['energy'],
            runtime=test_data['runtime']
        ))

    # Add summary at the top
    summary = f"""
        <div class="summary">
            <h2>Summary</h2>
            <p class="{('pass' if passed_tests == total_tests else 'fail')}">
                Test Status: {passed_tests}/{total_tests} testcases passed
            </p>
            <p>Total testcases: {total_tests}</p>
            <p>Passed testcases: {passed_tests}</p>
            <p>Failed testcases: {total_tests - passed_tests}</p>
            {f'<p class="fail">Failed tests: {", ".join(failed_tests)}</p>' if failed_tests else ''}
        </div>
    """

    html_content = html_template.format(
        timestamp=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        content=summary + '\n'.join(content)
    )

    with open(filename, 'w') as f:
        f.write(html_content)

def run_testcase(testcase_path):
    """Run a single testcase and return its output"""
    try:
        result = subprocess.run(["./simulator", testcase_path], 
                              stdout=subprocess.PIPE, 
                              stderr=subprocess.PIPE,
                              text=True)
        return result.stdout
    except subprocess.CalledProcessError as e:
        return f"Error running testcase: {str(e)}"
    
import multiprocessing
from concurrent.futures import ProcessPoolExecutor, as_completed

def run_parallel_testcases(testcases_dir, testcases):
    """Run testcases in parallel and return results"""
    results = {}
    max_workers = multiprocessing.cpu_count()  # Use number of CPU cores
    
    print(f"Running testcases in parallel using {max_workers} workers...")
    
    with ProcessPoolExecutor(max_workers=max_workers) as executor:
        # Submit all testcases
        future_to_testcase = {
            executor.submit(run_testcase, os.path.join(testcases_dir, testcase)): testcase 
            for testcase in testcases
        }
        
        # Process results as they complete
        for future in as_completed(future_to_testcase):
            testcase = future_to_testcase[future]
            try:
                output = future.result()
                
                # Parse results
                sla_results = []
                energy = None
                runtime = None
                
                for line in output.split('\n'):
                    if "SLA" in line and "compliance" in line:
                        sla_data = parse_sla_line(line)
                        if sla_data:
                            sla_results.append(sla_data)
                    elif "Total Energy" in line:
                        energy = extract_energy_and_runtime(line)
                    elif "Simulation run finished" in line:
                        runtime = extract_energy_and_runtime(line)

                results[testcase] = {
                    'sla_results': sorted(sla_results, key=lambda x: x['sla_id']),
                    'energy': energy,
                    'runtime': runtime
                }
                print(f"✓ Completed {testcase}")
            
            except Exception as e:
                print(f"✗ Failed {testcase}: {str(e)}")
                results[testcase] = {
                    'sla_results': [],
                    'energy': None,
                    'runtime': None,
                    'error': str(e)
                }
    
    return results

def extract_energy_and_runtime(line):
    """Extract energy or runtime from a line"""
    energy_match = re.search(r"Total Energy ([\d.]+)KW-Hour", line)
    if energy_match:
        return float(energy_match.group(1))
    
    runtime_match = re.search(r"finished in ([\d.]+) seconds", line)
    if runtime_match:
        return float(runtime_match.group(1))
    
    return None
def main():
    # Ensure simulator is built
    subprocess.run(["make", "simulator"])

    # Get all .md files from testcases directory
    testcases_dir = "testcases"
    testcases = [f for f in os.listdir(testcases_dir) if f.endswith('.md')]

    # Create results directory if it doesn't exist
    results_dir = "test_results"
    if not os.path.exists(results_dir):
        os.makedirs(results_dir)

    # Ask user for execution mode
    while True:
        mode = input("Choose execution mode (1 for serial, 2 for parallel): ").strip()
        if mode in ['1', '2']:
            break
        print("Invalid choice. Please enter 1 or 2.")

    # Ask for run name
    run_name = input("Enter a name for this test run (press Enter for timestamp): ").strip()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    
    # Generate filename based on input
    if run_name:
        filename = f"test_report_{run_name}_{timestamp}.html"
    else:
        filename = f"test_report_{timestamp}.html"
    
    html_file = os.path.join(results_dir, filename)

    if mode == '1':
        print("\nRunning tests in serial mode...")
        results = {}
        for testcase in sorted(testcases):
            print(f"Running {testcase}...")
            output = run_testcase(os.path.join(testcases_dir, testcase))
            
            # Parse results
            sla_results = []
            energy = None
            runtime = None
            
            for line in output.split('\n'):
                if "SLA" in line and "compliance" in line:
                    sla_data = parse_sla_line(line)
                    if sla_data:
                        sla_results.append(sla_data)
                elif "Total Energy" in line:
                    energy = extract_energy_and_runtime(line)
                elif "Simulation run finished" in line:
                    runtime = extract_energy_and_runtime(line)

            results[testcase] = {
                'sla_results': sorted(sla_results, key=lambda x: x['sla_id']),
                'energy': energy,
                'runtime': runtime
            }
    else:
        print("\nRunning tests in parallel mode...")
        results = run_parallel_testcases(testcases_dir, sorted(testcases))

    # Generate HTML report
    generate_html_report(results, html_file)
    print(f"\nTest report has been generated: {html_file}")
    
   
    

if __name__ == "__main__":
    main()